"""Host-side LLM loop tool — closes the product loop on the PC.

Product loop: ESP32 scans BLE ads -> JSON lines over USB CDC -> this tool
feeds a sample to an LLM -> the LLM generates a Lua filter/transform script
-> the script is deployed via the F4.2 upload protocol (SCRIPT LOAD/END) ->
the device runs on_adv/transform -> a cleaned stream comes back to the PC.

Subcommands:
  capture  SCAN START + collect advertisement JSON lines into a JSONL file
  analyze  send a capture to the LLM (or a bundled sample with --dry-run),
           write the Lua script to a file for review
  deploy   upload a Lua file, SCRIPT RUN, verify the output stream
  loop     capture -> analyze -> deploy -> verify in one run

LLM backend (OpenAI-compatible chat completions, urllib only):
  LLM_BASE_URL  default https://api.openai.com/v1 (DeepSeek, OpenRouter,
                Ollama, ... all work)
  LLM_API_KEY   bearer token (falls back to OPENAI_API_KEY; Ollama accepts
                any value)
  LLM_MODEL     default gpt-4o-mini
  The same three variables can instead live in a gitignored `.llm_env`
  file next to this script (KEY=value lines); real env vars take
  precedence.

The port stays OPEN for the whole run — closing it resets the chip
(N3, bug_check 2026-08-10). The tool leaves the device idle before exit
(SCRIPT STOP + SCAN STOP); the final port close at process exit still
resets the chip, same as the HIL suites.

Usage:
  python llm_loop.py [PORT] capture --secs 10 --out adv_capture.jsonl
  python llm_loop.py [PORT] analyze --in adv_capture.jsonl --out llm_script.lua
  python llm_loop.py [PORT] deploy --script llm_script.lua --verify-secs 8
  python llm_loop.py [PORT] loop --secs 8 [--goal "..."] [--dry-run]
"""
import argparse
import json
import os
import sys
import time
import urllib.error
import urllib.request

import serial

BAUD = 115200
MAX_SAMPLES = 30  # deduped adv lines sent to the LLM, bounds token cost

DEFAULT_GOAL = ("Suppress weak/noisy advertisers; keep only interesting "
                "devices and emit a compact JSON line with addr, name "
                "(when known) and rssi.")

SYSTEM_PROMPT = """You write Lua 5.4 scripts for an ESP32-S3 BLE sniffer dongle.
The script runs in a sandbox: only the string, table, math and utf8 libraries
exist. os, io, debug, dofile and require are absent and using them is
rejected. The script must stay under 8192 bytes and each hook must finish in
under 5 ms.

The device streams BLE advertisements as one JSON object per line:
{"addr":"AA:BB:CC:DD:EE:FF","type":"public|random","rssi":-70,"ts":123456,"name":"..."|null,"uuids":["180A"],"manu":{"id":"004C","data":"0215"}|null}

You may define either or both hooks:
- on_adv(addr, addr_type, rssi, name, uuids, manu_id, manu_data) -> boolean
  Called per advertisement. Return true to emit the device, false to suppress.
- transform(addr, json_string) -> string
  Called after on_adv passes. Return the exact JSON line to emit.

File-scope locals persist between calls (counters, sets). Reply with Lua
source only: no markdown fences, no explanations."""

DRY_RUN_SCRIPT = """-- dry-run sample: keep only strong advertisers, compact output
function on_adv(addr, addr_type, rssi, name, uuids, manu_id, manu_data)
    return rssi >= -80
end

function transform(addr, json_string)
    local r = json_string:match('"rssi":(-?%d+)')
    local n = json_string:match('"name":"([^"]*)"')
    local out = '{"addr":"' .. addr .. '","rssi":' .. (r or '0')
    if n then
        out = out .. ',"name":"' .. n .. '"'
    end
    return out .. '}'
end
"""


def expected_cmd(line):
    """Response 'cmd' field for a CLI command line ('CONN TARGET x' ->
    'conn_target'); None when unknown. cmd_json matches responses by
    this so a stale status line from a previous exchange can never
    satisfy the wrong command (2026-08-28 stale-line lesson)."""
    words = line.split()
    if len(words) >= 2 and words[0] in ("SCAN", "CONN", "SCRIPT", "POWER"):
        return (words[0] + "_" + words[1]).lower()
    if words and words[0] in ("STATUS", "VERSION"):
        return words[0].lower()
    return None


def cmd_json(s, line, timeout=4.0):
    """Send one command line; return (parsed JSON with 'status', all lines).
    Responses are matched by their cmd field to the command issued."""
    want = expected_cmd(line)
    s.reset_input_buffer()
    s.write((line + "\n").encode())
    s.flush()
    end = time.time() + timeout
    lines = []
    while time.time() < end:
        raw = s.readline()
        if not raw:
            continue
        txt = raw.decode(errors="replace").strip()
        if not txt:
            continue
        lines.append(txt)
        if txt.startswith("{") and '"status"' in txt:
            try:
                obj = json.loads(txt)
            except json.JSONDecodeError:
                continue
            if isinstance(obj, dict) and (
                    want is None or obj.get("cmd") == want):
                return obj, lines
    return None, lines


def collect_adv(s, secs):
    """Collect advertisement JSON lines for secs seconds (port stays open)."""
    advs = []
    console = []
    end = time.time() + secs
    s.timeout = 0.5
    while time.time() < end:
        raw = s.readline()
        if not raw:
            continue
        txt = raw.decode(errors="replace").strip()
        if not txt:
            continue
        if txt.startswith("{") and '"addr"' in txt:
            try:
                advs.append(json.loads(txt))
                continue
            except json.JSONDecodeError:
                pass
        console.append(txt)
    s.timeout = 1
    return advs, console


def upload_script(s, script):
    """Upload a script via the F4.2 text-line bridge (SCRIPT LOAD).
    The bridge scans every data line fail-closed and answers a
    violating line IMMEDIATELY with -612 (then resets the session), so
    the send loop watches for that mid-upload response instead of
    reporting the misleading -611 from the SCRIPT END that follows
    (converged with the stage-5 tools, 2026-08-29)."""
    r, _ = cmd_json(s, "SCRIPT LOAD")
    if not r or r.get("status") != "ok":
        return False, f"SCRIPT LOAD: {r}"
    old_timeout = s.timeout
    s.timeout = 0.12
    try:
        for line in script.splitlines():
            s.write((line + "\n").encode())
            s.flush()
            time.sleep(0.05)
            while True:
                raw = s.readline()
                if not raw:
                    break
                txt = raw.decode(errors="replace").strip()
                if txt.startswith("{") and '"status":"error"' in txt:
                    return False, txt
    finally:
        s.timeout = old_timeout
    time.sleep(0.3)
    r, _ = cmd_json(s, "SCRIPT END")
    return bool(r and r.get("status") == "ok"), str(r)


def ensure_scanning(s):
    r, _ = cmd_json(s, "STATUS")
    if r and r.get("scanning"):
        return True
    r, _ = cmd_json(s, "SCAN START")
    return bool(r and r.get("status") == "ok")


def stop_all(s):
    r, _ = cmd_json(s, "SCRIPT STOP")
    if r and r.get("status") == "ok":
        print("  (script stopped)")
    r, _ = cmd_json(s, "SCAN STOP")
    if r and r.get("status") == "ok":
        print("  (scan stopped)")


def dedup_samples(advs):
    seen = set()
    out = []
    for a in advs:
        if a.get("addr") in seen:
            continue
        seen.add(a.get("addr"))
        out.append(a)
        if len(out) >= MAX_SAMPLES:
            break
    return out


def strip_fences(text):
    text = text.strip()
    if text.startswith("```"):
        text = text.split("\n", 1)[1] if "\n" in text else ""
        text = text.rsplit("```", 1)[0]
    return text.strip()


def load_env_file():
    """Optional .llm_env next to this script; real env vars take precedence."""
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), ".llm_env")
    env = {}
    if os.path.exists(path):
        with open(path, encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith("#") or "=" not in line:
                    continue
                k, v = line.split("=", 1)
                env[k.strip()] = v.strip().strip('"')
    return env


def resolve_llm_config(force_file=False):
    """Return {'base','key','model'} or None when no key is configured.

    Same rules as the stage-5 host tools (converged 2026-08-29): real
    env vars win as a UNIT (if any LLM_* variable is set, the whole
    config comes from the environment); otherwise .llm_env is used
    as-is — either the legacy LLM_* triple or provider pairs
    (DASHSCOPE_*, then TOKEN_PLAN_*; first complete pair wins); the
    model is LLM_MODEL or QWEN_MODEL. force_file skips the
    environment (used by the 401 fallback)."""
    file_env = load_env_file()
    env = os.environ
    if not force_file and (
            env.get("LLM_BASE_URL") or env.get("LLM_API_KEY")
            or env.get("OPENAI_API_KEY") or env.get("LLM_MODEL")):
        src, base = env, env.get("LLM_BASE_URL")
        key = src.get("LLM_API_KEY") or src.get("OPENAI_API_KEY") or ""
    else:
        src = file_env
        key = file_env.get("LLM_API_KEY") \
            or file_env.get("OPENAI_API_KEY") or ""
        base = file_env.get("LLM_BASE_URL")
        if not key:
            for prefix in ("DASHSCOPE", "TOKEN_PLAN"):
                b = file_env.get(prefix + "_BASE_URL")
                k = file_env.get(prefix + "_API_KEY")
                if b and k:
                    base, key = b, k
                    break
    if not key:
        return None
    model = (src.get("LLM_MODEL") or file_env.get("QWEN_MODEL")
             or "gpt-4o-mini")
    return {"base": (base or "https://api.openai.com/v1").rstrip("/"),
            "key": key, "model": model,
            "no_thinking": (os.environ.get("QWEN_ENABLE_THINKING")
                            or file_env.get("QWEN_ENABLE_THINKING",
                                           "")).lower() == "false"}


def _generate_once(cfg, messages):
    body = {"model": cfg["model"], "temperature": 0.2,
            "messages": messages}
    if cfg.get("no_thinking"):
        body["enable_thinking"] = False
    body = json.dumps(body).encode()
    req = urllib.request.Request(
        cfg["base"] + "/chat/completions", data=body,
        headers={"Content-Type": "application/json",
                 "Authorization": "Bearer " + cfg["key"]})
    try:
        with urllib.request.urlopen(req, timeout=180) as resp:
            out = json.load(resp)
    except urllib.error.HTTPError as e:
        raise RuntimeError(f"LLM HTTP {e.code}: "
                           f"{e.read()[:300].decode(errors='replace')}")
    except urllib.error.URLError as e:
        raise RuntimeError(f"LLM unreachable: {e.reason}")
    except TimeoutError:
        raise RuntimeError("LLM timeout: no response within the limit")
    return out["choices"][0]["message"]["content"]


def llm_generate(samples, goal):
    cfg = resolve_llm_config()
    if cfg is None:
        sys.exit("error: no LLM_API_KEY (or OPENAI_API_KEY) in the environment\n"
                 "       or in .llm_env (legacy LLM_* triple or DASHSCOPE_*/\n"
                 "       TOKEN_PLAN_* provider pairs) next to this script.\n"
                 "       Set LLM_BASE_URL/LLM_API_KEY/LLM_MODEL for your provider,\n"
                 "       or pass --dry-run to use the bundled sample script.")
    user = ("Goal: " + goal + "\n\nSample advertisements from the live "
            "environment (one JSON per line):\n"
            + "\n".join(json.dumps(a) for a in samples))
    messages = [
        {"role": "system", "content": SYSTEM_PROMPT},
        {"role": "user", "content": user},
    ]
    print(f"  (LLM: {cfg['model']}, {len(user)} chars of sample)")
    try:
        reply = _generate_once(cfg, messages)
    except RuntimeError as e:
        # Two known traps self-heal (same rules as the stage-5 tools):
        # an ambient shell key 401s against the wrong provider, and a
        # base ending /api/v1 is the native-dialect root, not the
        # OpenAI-compatible route.
        healed = False
        if "HTTP 401" in str(e):
            file_cfg = resolve_llm_config(force_file=True)
            if file_cfg and file_cfg["key"] != cfg["key"]:
                print(f"  (llm key from the shell environment was rejected "
                      f"(401) — switching to .llm_env [{file_cfg['model']}])")
                cfg = file_cfg
                healed = True
        if not healed and "HTTP 404" in str(e) and "/api/v1" in cfg["base"]:
            new_base = cfg["base"].rsplit("/api/v1", 1)[0] \
                + "/compatible-mode/v1"
            print(f"  (base {cfg['base']} answered 404 (native dialect "
                  f"root) — retrying on {new_base})")
            cfg["base"] = new_base
            healed = True
        if not healed:
            sys.exit(f"error: {e}")
        reply = _generate_once(cfg, messages)
    return strip_fences(reply)


def report_stream(advs, console, label):
    print(f"  [{label}] {len(advs)} adv lines in window "
          f"(console lines: {len(console)})")
    for a in advs[:3]:
        print(f"    sample: {json.dumps(a)}")
    for c in console[:3]:
        print(f"    console: {c}")


def do_capture(s, args):
    if not ensure_scanning(s):
        sys.exit("error: SCAN START failed")
    print(f"capturing {args.secs}s of advertisements...")
    advs, _ = collect_adv(s, args.secs)
    with open(args.out, "w", encoding="utf-8") as f:
        for a in advs:
            f.write(json.dumps(a) + "\n")
    print(f"wrote {len(advs)} adv lines to {args.out}")
    stop_all(s)


def do_analyze(s, args):
    if args.dry_run:
        script = DRY_RUN_SCRIPT
        print("dry-run: using bundled sample script (no LLM call)")
    else:
        with open(args.src, encoding="utf-8") as f:
            advs = [json.loads(line) for line in f if line.strip()]
        samples = dedup_samples(advs)
        if not samples:
            sys.exit(f"error: no adv lines in {args.src}")
        print(f"analyzing {len(samples)} deduped sample lines...")
        script = llm_generate(samples, args.goal)
    with open(args.out, "w", encoding="utf-8") as f:
        f.write(script + "\n")
    print(f"script written to {args.out} ({len(script)} bytes):\n{script}")


def do_deploy(s, args):
    with open(args.script, encoding="utf-8") as f:
        script = f.read()
    if not ensure_scanning(s):
        sys.exit("error: SCAN START failed")
    print(f"baseline ({args.verify_secs}s)...")
    before, _ = collect_adv(s, args.verify_secs)
    report_stream(before, [], "before")
    ok, detail = upload_script(s, script)
    if not ok:
        stop_all(s)
        sys.exit(f"error: upload rejected: {detail}")
    r, _ = cmd_json(s, "SCRIPT RUN")
    if not r or r.get("status") != "ok":
        stop_all(s)
        sys.exit(f"error: SCRIPT RUN failed: {r}")
    print(f"script running, verifying {args.verify_secs}s...")
    after, console = collect_adv(s, args.verify_secs)
    report_stream(after, console, "after")
    print(f"  rate: {len(before)} -> {len(after)} adv lines per "
          f"{args.verify_secs:.0f}s")
    stop_all(s)


def do_loop(s, args):
    if not ensure_scanning(s):
        sys.exit("error: SCAN START failed")
    print(f"[1/4] capturing {args.secs}s baseline...")
    advs, _ = collect_adv(s, args.secs)
    report_stream(advs, [], "baseline")
    if not advs:
        stop_all(s)
        sys.exit("error: no ambient BLE traffic captured; nothing to analyze")
    print("[2/4] generating Lua script...")
    if args.dry_run:
        script = DRY_RUN_SCRIPT
        print("  dry-run: bundled sample script")
    else:
        script = llm_generate(dedup_samples(advs), args.goal)
    print(f"  script ({len(script)} bytes):\n{script}")
    print("[3/4] deploying...")
    ok, detail = upload_script(s, script)
    if not ok:
        stop_all(s)
        sys.exit(f"error: upload rejected: {detail}")
    r, _ = cmd_json(s, "SCRIPT RUN")
    if not r or r.get("status") != "ok":
        stop_all(s)
        sys.exit(f"error: SCRIPT RUN failed: {r}")
    print(f"[4/4] verifying {args.secs}s of cleaned stream...")
    after, console = collect_adv(s, args.secs)
    report_stream(after, console, "cleaned")
    print(f"  rate: {len(advs)} -> {len(after)} adv lines per {args.secs:.0f}s")
    stop_all(s)
    print("loop complete.")


def main():
    p = argparse.ArgumentParser(
        description="Host-side LLM loop for the BLE sniffer dongle "
                    "(spec: harness/01-features/stage5-host/).")
    p.add_argument("port", nargs="?", default="COM12",
                   help="serial port (default COM12)")
    sub = p.add_subparsers(dest="cmd", required=True)

    c = sub.add_parser("capture", help="collect adv JSON lines to a file")
    c.add_argument("--secs", type=float, default=10)
    c.add_argument("--out", default="adv_capture.jsonl")

    a = sub.add_parser("analyze", help="LLM-generate a Lua script from a capture")
    a.add_argument("--in", dest="src", default="adv_capture.jsonl")
    a.add_argument("--out", default="llm_script.lua")
    a.add_argument("--goal", default=DEFAULT_GOAL)
    a.add_argument("--dry-run", action="store_true",
                   help="skip the LLM, use the bundled sample script")

    d = sub.add_parser("deploy", help="upload a Lua file and verify the stream")
    d.add_argument("--script", default="llm_script.lua")
    d.add_argument("--verify-secs", type=float, default=8)

    l = sub.add_parser("loop", help="capture -> analyze -> deploy -> verify")
    l.add_argument("--secs", type=float, default=8)
    l.add_argument("--goal", default=DEFAULT_GOAL)
    l.add_argument("--dry-run", action="store_true",
                   help="skip the LLM, use the bundled sample script")

    args = p.parse_args()
    s = None
    if args.cmd != "analyze":  # analyze is offline: file -> LLM -> file
        s = serial.Serial(args.port, BAUD, timeout=1)
        time.sleep(0.5)
        s.reset_input_buffer()
        print(f"=== llm_loop on {args.port} ({args.cmd}) ===")
    else:
        print(f"=== llm_loop ({args.cmd}, offline) ===")
    try:
        {"capture": do_capture, "analyze": do_analyze,
         "deploy": do_deploy, "loop": do_loop}[args.cmd](s, args)
    finally:
        if s:
            s.close()


if __name__ == "__main__":
    main()
