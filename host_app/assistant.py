"""Interactive LLM assistant session for the sniffer dongle (H5.3).

A message Prompt (event loop) multiplexes two inputs onto one loop:
the dongle's USB JSON lines and the operator's console input. While the
user thinks, the loop keeps draining the serial port (N3 keep-open), so
the rolling device buffer stays fresh and the device never blocks.

  ESP32 --USB JSON lines--> +--------------------+
 user  --console input---> | message Prompt     | --> context assembly --> cloud LLM
                           | ingress validation |     (history + snapshot)
                           | bounded buffers    | <-- typed JSON envelope
                           +--------------------+

Typed envelope contract (the LLM must reply with JSON only):
  {"type":"answer","text":"..."}              plain reply over the data
  {"type":"lua","text":"why","code":"--..."}  artifact -> deploy? [y/N]
  {"type":"clarify","text":"question"}         mixed initiative: loop continues
  {"type":"error","text":"..."}                e.g. "insufficient data"

A reply that is not a valid envelope gets ONE retry with an
escaping-focused correction (lua.code is the highest-risk field); if
strict parsing still fails, a fenced ```lua block is extracted before
erroring to the user. Lua artifacts deploy via SCRIPT LOAD/END/RUN only
after an explicit human "y" (the device sandbox scan stays the final
gate; conn lines are analysis-only and bypass the Lua hooks - F2.4).

Session surface:
  <free text>      tasking for the LLM
  /samples [n]     show the current device buffer snapshot
  /scan on|off     control the advertisement plane
  /conn on|off     control the connection plane (CONN START/STOP)
  /deploy          re-offer the last Lua artifact
  /tools           list registered tool packs (H6.1 tool registry)
  /tools refresh   re-read the manifest from the device
  /tools load F    register a tool pack (confirm-gated; H6.1)
  /history         conversation summary
  /help, /quit     ...

For prompt experiments: --system-extra FILE appends to the built-in
system prompt (keeps the envelope contract); --system-file FILE
replaces it outright (the typed-envelope router stays active, so
non-envelope replies surface as clean errors, never a crash).

Tool registry (H6.1 M1, host-only): a tool pack is a Lua file defining
functions plus a manifest() describing them (one complete statement per
line, <= 240 B). /tools load registers a pack on the device in RUN MODE:
each line is LUA EXEC-ed into the live Lua state - RAM only, nothing
persisted, the flash slot and any running filter script are untouched.
The host fail-closed scans every line first (the device bridge's own
token list) and validates the manifest (fetched in string.sub chunks -
the 256 B result path cannot carry a whole manifest). While tools are
registered, a "lua" envelope that composes tools (and defines no
on_adv/transform/manifest) is a TOOL PROGRAM: executed on the device
immediately - autonomously, no deploy confirm (decision 10) - and its
result string is fed back to the LLM for the final answer; consecutive
executions are capped per user turn. Filter/pack artifacts keep the
human deploy gate.

LLM backend (same conventions as llm_loop.py, values may live in a
gitignored .llm_env next to this script or in the repo root):
  LLM_BASE_URL  default https://api.openai.com/v1
  LLM_API_KEY   bearer token (falls back to OPENAI_API_KEY)
  LLM_MODEL     default gpt-4o-mini
The file may instead carry provider pairs (DASHSCOPE_BASE_URL/API_KEY,
then TOKEN_PLAN_*; model from QWEN_MODEL) - legacy LLM_* triple first,
then the pairs, first complete pair wins (same rule as run_case.py).
Real env vars win as a UNIT (if any is set, the whole config comes from
the environment); the file is used only when the environment is silent,
so a foreign provider's key never mixes with the file's base URL.

Every run is tee'd to assistant_<timestamp>.log in the current
directory (review evidence). The port stays open for the whole session;
the final close at orderly exit resets the chip, same as the HIL suites.

Usage:
  python host_app/assistant.py [port] [--no-llm]
"""
import argparse
import json
import os
import re
import sys
import time
import urllib.error
import urllib.request
from collections import deque

import serial

BAUD = 115200

ADV_BUF_MAX = 60        # rolling adv-plane cap (sample; review answer R3)
CONN_BUF_MAX = 200      # rolling conn-plane cap (keep all recent conn)
CONSOLE_KEEP = 20       # recent non-JSON diagnostics kept for /samples
HISTORY_CHAR_BUDGET = 20000  # ~5k tokens; oldest turns trimmed (R-answer 1)
SNAPSHOT_ADV_MAX = 20   # deduped-by-addr adv lines shown to the LLM
SNAPSHOT_CONN_MAX = 30  # conn lines shown to the LLM
DRAIN_TIMEOUT = 0.05    # serial readline timeout inside the Prompt loop
LLM_TIMEOUT_S = 180

# ---- H6.1 tool registry (M1: host-only, zero firmware) ----------------------

TOOL_PACKS_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                              "tool_packs")
LUA_EXEC_LINE_MAX = 240   # code budget/line: 256 B USB RX - "LUA EXEC " - \n
MANIFEST_CHUNK = 180      # string.sub slice/fetch: 256 B result, 512 B TX
MANIFEST_MAX = 8192       # hard cap on one manifest string
MANIFEST_VERSION = 1
TOOL_EXEC_CAP = 3         # consecutive tool-program executions per turn
TOOL_PROGRAM_MAX_LINES = 12
ARG_TYPES = ("string", "number", "boolean", "table")
FORBIDDEN_DOTTED = ("os.", "io.", "debug.", "package.")
FORBIDDEN_WORDS = ("dofile", "loadfile", "load", "require", "collectgarbage")
RESERVED_TOOL_NAMES = ("manifest", "on_adv", "transform")
IDENT_RE = re.compile(r"^[A-Za-z_]\w*$")
DEPLOY_RE = re.compile(
    r"function\s+(?:on_adv|transform|manifest)\b"
    r"|(?:on_adv|transform|manifest)\s*=\s*function")

SYSTEM_PROMPT = """You are the assistant inside a host-side session for an
ESP32-S3 BLE sniffer dongle. The dongle streams JSON lines over USB; a
snapshot of the most recent lines is attached to each user message.

DATA PLANES AND MODE BOUNDARY (mandatory):
- Advertisement lines look like
  {"addr":"AA:BB:CC:DD:EE:FF","type":"public|random","rssi":-70,"ts":123,"name":"..."|null,"uuids":["180A"],"manu":{"id":"004C","data":"0215"}|null}
  On this plane the device can run a Lua 5.4 sandbox script, so for
  advertisement-plane requests you MAY reply with type "lua".
- Connection lines carry "src":"conn" (envelope
  {"ts":...,"addr":"...","src":"conn",...} plus the peer's payload).
  They deliberately bypass the Lua hooks: connection data is ANALYSIS
  ONLY. Never offer a Lua script for conn-plane questions; reply with
  "answer" or "clarify" instead.

LUA SANDBOX (only for type "lua"):
- Only the string, table, math and utf8 libraries exist. os, io, debug,
  dofile and require are absent and using them is rejected by the device.
- The script must stay under 8192 bytes and each hook must finish in
  under 5 ms.
- Hooks (either or both; file-scope locals persist between calls):
  on_adv(addr, addr_type, rssi, name, uuids, manu_id, manu_data) -> boolean
    called per advertisement; true emits the device, false suppresses it
  transform(addr, json_string) -> string
    called after on_adv passes; returns the exact outgoing JSON line

RESPONSE CONTRACT - reply with ONE JSON object and NOTHING else (no
markdown fences, no text before or after), in this envelope:
{"type": "answer",  "text": "..."}
{"type": "lua",     "text": "why this script", "code": "-- Lua 5.4 source"}
{"type": "clarify", "text": "question to ask the user"}
{"type": "error",   "text": "..."}

The reply is JSON: inside "code" every newline must be written \\n, every
double quote \\", every backslash \\\\. Choose "clarify" when the request
is ambiguous and you need information from the user; choose "error" when
the request cannot be met. Be concise."""

ESCAPE_RETRY_MSG = (
    "Your previous reply was not a valid JSON envelope. Most likely the "
    "lua code contained raw newlines or unescaped quotes/backslashes. "
    "Reply again with ONE JSON object only, no markdown fences: re-emit "
    "the same envelope, and inside the code string escape every newline "
    "as \\n, every double quote as \\\" and every backslash as \\\\. "
    "Do not change the script itself.")

ENVELOPE_TYPES = ("answer", "lua", "clarify", "error")


# ---- LLM backend (copied from llm_loop.py per house self-contained style) --

def load_env_file():
    """Optional .llm_env next to this script, then in the repo root;
    real env vars take precedence."""
    env = {}
    here = os.path.dirname(os.path.abspath(__file__))
    for path in (os.path.join(here, ".llm_env"),
                 os.path.join(os.path.dirname(here), ".llm_env")):
        if os.path.exists(path):
            with open(path, encoding="utf-8") as f:
                for line in f:
                    line = line.strip()
                    if not line or line.startswith("#") or "=" not in line:
                        continue
                    k, v = line.split("=", 1)
                    env[k.strip()] = v.strip().strip('"')
            break
    return env


def resolve_llm_config(force_file=False):
    """Return {'base','key','model'} or None when no key is configured.

    One coherent source wins: if any of the LLM_* variables is set in
    the real environment, the whole config comes from the environment
    (with defaults); otherwise the .llm_env file is used as-is - it may
    carry the legacy LLM_* triple OR provider pairs (DASHSCOPE_*,
    TOKEN_PLAN_* - resolved in that order, first complete pair wins),
    and the model is LLM_MODEL or QWEN_MODEL (same rule as run_case.py,
    ported 2026-08-28 when .llm_env moved to the pair scheme).
    Per-variable mixing is avoided on purpose - an ambient env key from
    a different provider combined with this file's base URL is a
    guaranteed 401 (observed 2026-08-28: an expired bigmodel key in the
    shell env overrode a perfectly valid dashscope key in .llm_env).
    force_file=True skips the environment entirely (used by the 401
    fallback when an ambient shell key hijacked the config)."""
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
    return {
        "base": (base or "https://api.openai.com/v1").rstrip("/"),
        "key": key,
        "model": model,
        # honor QWEN_ENABLE_THINKING=false from the file: thinking
        # models otherwise burn minutes on tool prompts (observed
        # 2026-08-29: qwen3.8-27b timed out at 180 s with thinking on)
        "no_thinking": (os.environ.get("QWEN_ENABLE_THINKING")
                        or file_env.get("QWEN_ENABLE_THINKING",
                                       "")).lower() == "false",
    }


def _chat_once(cfg, messages):
    """One raw chat-completions request; never heals."""
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
        with urllib.request.urlopen(req, timeout=LLM_TIMEOUT_S) as resp:
            out = json.load(resp)
    except urllib.error.HTTPError as e:
        raise RuntimeError(f"LLM HTTP {e.code}: "
                           f"{e.read()[:300].decode(errors='replace')}")
    except urllib.error.URLError as e:
        raise RuntimeError(f"LLM unreachable: {e.reason}")
    except TimeoutError:
        raise RuntimeError("LLM timeout: no response within the limit")
    return out["choices"][0]["message"]["content"]


def llm_chat(cfg, messages, note=None):
    """One chat-completions round trip; returns the content string.

    Self-heals ONE recurring config mistake: a 404 from a base ending
    in /api/v1 means the native DashScope-dialect root was configured
    (the Aliyun console hands that path out); the OpenAI-compatible
    route on the same host is /compatible-mode/v1. The heal retries
    once, is announced via note(), and sticks in cfg for the session
    (observed twice on this machine, 2026-08-28)."""
    try:
        return _chat_once(cfg, messages)
    except RuntimeError as e:
        if "HTTP 404" not in str(e) or "/api/v1" not in cfg["base"]:
            raise
        healed = cfg["base"].rsplit("/api/v1", 1)[0] + "/compatible-mode/v1"
        if note is not None:
            note(f"(base {cfg['base']} answered 404 (native dialect root) "
                 f"- retrying on {healed})")
        cfg["base"] = healed
        return _chat_once(cfg, messages)


# ---- Typed envelope parsing (defensive; Rec1 retry + fenced fallback) ------

def strip_outer_fence(text):
    """Drop one wrapping ``` / ```json fence, if present."""
    text = text.strip()
    if text.startswith("```"):
        text = text.split("\n", 1)[1] if "\n" in text else ""
        text = text.rsplit("```", 1)[0]
    return text.strip()


def parse_envelope(text, allow_fenced=True):
    """Parse an LLM reply into the typed envelope.
    Returns (envelope dict | None, used_fenced_fallback bool).
    Strict JSON first; the fenced ```lua extraction is gated by
    allow_fenced because it is the LAST resort - the caller gives the
    model ONE escaping-focused retry before resorting to it (spec Rec1)."""
    candidate = strip_outer_fence(text)
    try:
        obj = json.loads(candidate)
    except (json.JSONDecodeError, ValueError):
        obj = None
    if isinstance(obj, dict):
        t = obj.get("type")
        if t in ENVELOPE_TYPES and isinstance(obj.get("text"), str):
            if t == "lua" and not isinstance(obj.get("code"), str):
                obj = None  # lua without code is malformed -> retry path
            else:
                return obj, False
    if not allow_fenced:
        return None, False
    # Fenced-lua fallback: the reply was not a valid envelope, but the
    # artifact may still be recoverable from a ```lua block.
    m = re.search(r"```lua\r?\n(.*?)```", text, re.DOTALL)
    if m:
        return {"type": "lua",
                "text": "extracted from a fenced lua block "
                        "(the raw reply was not a valid JSON envelope)",
                "code": m.group(1).strip()}, True
    return None, False


def compose_system_prompt(replace_path=None, extra_path=None):
    """Build the session's system prompt from optional overrides.
    Returns (prompt_text, description-for-the-banner). --system-file
    replaces the built-in prompt entirely (the typed-envelope router
    stays active, so a replacement that drops the envelope contract
    will surface as clean parse errors, never a crash); --system-extra
    is appended to the built-in prompt - the safe way to steer
    style/persona/behavior for an experiment."""
    prompt, note = SYSTEM_PROMPT, "built-in"
    if replace_path:
        with open(replace_path, encoding="utf-8") as f:
            prompt = f.read().strip()
        note = "replaced by " + replace_path
    if extra_path:
        with open(extra_path, encoding="utf-8") as f:
            prompt = prompt + "\n\n" + f.read().strip()
        note = (note + " + extra " + extra_path) if replace_path \
            else ("built-in + extra " + extra_path)
    return prompt, note


def build_messages(history_view, snapshot, user_text,
                   system_prompt=SYSTEM_PROMPT):
    """Assemble ONE request: system prompt + trimmed history + a reduced
    device-buffer view + the user's prompt (spec 2.3 item 5)."""
    content = user_text
    if snapshot:
        content = "Device data snapshot (most recent lines first):\n" \
                  + snapshot + "\n\nUser request: " + user_text
    return ([{"role": "system", "content": system_prompt}]
            + list(history_view)
            + [{"role": "user", "content": content}])


# ---- H6.1 tool registry: pack convention + host-side validation (M1) --------

def scan_lua_line(line):
    """Bridge-parity sandbox scan (the lua_llm_bridge.c token list):
    dotted tokens match anywhere (they can only be table indexing), bare
    words need identifier boundaries; a token inside a string literal is
    rejected too - the same deliberate fail-closed stance the device
    bridge takes with untrusted input. Returns the token or None."""
    for tok in FORBIDDEN_DOTTED:
        if tok in line:
            return tok
    for tok in FORBIDDEN_WORDS:
        if re.search(r"(?<!\w)" + tok + r"(?!\w)", line):
            return tok
    return None


def lua_exec_lines(src, kind="pack", max_lines=None):
    """Split Lua source into LUA EXEC lines under the pack convention:
    every kept line must be ONE complete statement; comment/blank lines
    are skipped and never sent. Returns (lines, None) or (None, reason)."""
    lines = []
    for i, raw in enumerate(src.splitlines(), 1):
        line = raw.rstrip()
        s = line.strip()
        if not s or s.startswith("--"):
            continue
        tok = scan_lua_line(s)
        if tok:
            return None, (f"{kind} rejected: line {i} contains forbidden "
                          f"token '{tok}' (sandbox scan, fail-closed)")
        if len(line) > LUA_EXEC_LINE_MAX:
            return None, (f"{kind} rejected: line {i} exceeds the "
                          f"{LUA_EXEC_LINE_MAX}-byte LUA EXEC line budget")
        lines.append(line)
        if max_lines is not None and len(lines) > max_lines:
            return None, (f"{kind} rejected: more than {max_lines} "
                          "executable lines")
    if not lines:
        return None, f"{kind} rejected: no executable lines"
    return lines, None


def _check_arg(a):
    if not isinstance(a, dict) or not isinstance(a.get("name"), str) \
            or not IDENT_RE.match(a["name"]):
        return "each arg must be an object with a Lua-safe 'name'"
    if a.get("type") not in ARG_TYPES:
        return f"arg '{a['name']}': type must be one of {ARG_TYPES}"
    if a.get("item_type") is not None \
            and a["item_type"] not in ARG_TYPES:
        return f"arg '{a['name']}': bad item_type"
    return None


def validate_manifest(text):
    """Host-side manifest validation (decision 9). Returns
    (manifest_dict, None) or (None, reason)."""
    try:
        obj = json.loads(text)
    except (json.JSONDecodeError, ValueError):
        return None, "manifest is not valid JSON"
    if not isinstance(obj, dict):
        return None, "manifest is not a JSON object"
    if obj.get("version") != MANIFEST_VERSION:
        return None, f"manifest version must be {MANIFEST_VERSION}"
    name = obj.get("name")
    if not isinstance(name, str) or not IDENT_RE.match(name):
        return None, "manifest 'name' must be a Lua-safe identifier"
    tools = obj.get("tools")
    if not isinstance(tools, list) or not tools:
        return None, "manifest 'tools' must be a non-empty list"
    seen = set()
    for t in tools:
        if not isinstance(t, dict):
            return None, "each tool must be an object"
        n = t.get("name")
        if not isinstance(n, str) or not IDENT_RE.match(n):
            return None, "tool name must be a Lua-safe identifier"
        if n in seen:
            return None, f"duplicate tool name '{n}'"
        if n in RESERVED_TOOL_NAMES:
            return None, f"tool name '{n}' is reserved"
        seen.add(n)
        if not isinstance(t.get("doc"), str) or not t["doc"].strip():
            return None, f"tool '{n}': 'doc' is required"
        args = t.get("args", [])
        if not isinstance(args, list):
            return None, f"tool '{n}': 'args' must be a list"
        for a in args:
            err = _check_arg(a)
            if err:
                return None, f"tool '{n}': {err}"
        if "mutating" in t and not isinstance(t["mutating"], bool):
            return None, f"tool '{n}': 'mutating' must be a boolean"
    return obj, None


def assemble_manifest_from_source(src):
    """Reconstruct a pack's manifest JSON host-side from its source, so it
    can be validated (and name-collision-checked) BEFORE any line is sent
    to the device (H6.1 AC#2). The manifest() is assembled from
    string-concatenation lines (VAR = [[...]] / VAR = VAR .. [[...]]), so
    those literals can be re-joined here. Returns (text, None) or
    (None, reason)."""
    var = None
    for ln in src.splitlines():
        m = re.match(r"^function\s+manifest\s*\(\s*\)\s*return"
                     r"\s+([A-Za-z_]\w*)", ln)
        if m:
            var = m.group(1)
            break
    if var is None:
        return None, "no manifest() function found in source"
    parts = None
    for ln in src.splitlines():
        v = re.escape(var)
        m = re.match(r"^%s\s*=\s*\[\[(.*)\]\]$" % v, ln)
        if m:
            parts = [m.group(1)]
            continue
        if parts is not None:
            m = re.match(r"^%s\s*=\s*%s\s*\.\.\s*\[\[(.*)\]\]$" % (v, v), ln)
            if m:
                parts.append(m.group(1))
    if parts is None:
        return None, f"no '{var}' string literal found in source"
    return "".join(parts), None


def _arg_sig(t):
    parts = []
    for a in t.get("args", []):
        s = f"{a['name']}: {a['type']}"
        if "enum" in a:
            s += " (" + "|".join(str(x) for x in a["enum"]) + ")"
        parts.append(s)
    return f"{t['name']}({', '.join(parts)})"


def _arg_call_sig(t):
    """A runnable example call built from the manifest's example args,
    e.g. mean({numbers=[3, 5, 10]}). JSON scalars are valid Lua here."""
    ea = (t.get("example") or {}).get("args")
    if isinstance(ea, dict) and ea:
        inner = ", ".join(k + "=" + json.dumps(v) for k, v in ea.items())
        return f"{t['name']}({{{inner}}})"
    return t["name"] + "({})"


class ToolRegistry:
    """Session-scoped registry (decision 13: run mode - a reboot clears
    the device side). The HOST cache is authoritative: on-device a later
    pack's manifest() overwrites the earlier one's global, so manifests
    are captured at load time (decision 9 keeps validation host-side)."""

    def __init__(self):
        self.packs = []

    def is_empty(self):
        return not self.packs

    def tool_names(self):
        return {t["name"] for p in self.packs for t in p["tools"]}

    def add(self, manifest, path):
        if any(p["name"] == manifest["name"] for p in self.packs):
            return None, f"pack '{manifest['name']}' is already registered"
        names = {t["name"] for t in manifest["tools"]}
        clash = names & (self.tool_names() | set(RESERVED_TOOL_NAMES))
        if clash:
            return None, ("name collision with an active pack: "
                          + ", ".join(sorted(clash)))
        pack = {"name": manifest["name"], "file": path,
                "tools": manifest["tools"]}
        self.packs.append(pack)
        return pack, None

    def describe(self):
        out = [f"tools: {len(self.packs)} pack(s), "
               f"{len(self.tool_names())} tool(s) "
               "(session-scoped: re-load after a reboot)"]
        for p in self.packs:
            out.append(f"  {p['name']}  ({p['file']})")
            for t in p["tools"]:
                mark = "  [mutating]" if t.get("mutating") else ""
                out.append(f"    {_arg_sig(t)} -> "
                           f"{t.get('returns', 'string')}{mark}")
                out.append("      " + t["doc"])
                if t.get("example"):
                    out.append(f'      example: {_arg_call_sig(t)} -> "'
                               + str(t["example"].get("result", "..."))
                               + '"')
        return "\n".join(out)

    def prompt(self):
        """TOOLS system-prompt section; empty string when nothing is
        registered (H5.3 behavior is then exactly unchanged)."""
        if not self.packs:
            return ""
        out = ["", "TOOL REGISTRY (functions registered on the device by",
               "the operator; session-scoped):"]
        for p in self.packs:
            out.append(f'pack "{p["name"]}" ({p["file"]}):')
            for t in p["tools"]:
                mark = " [mutating]" if t.get("mutating") else ""
                out.append(f"- {_arg_sig(t)} -> "
                           f"{t.get('returns', 'string')}{mark}: {t['doc']}")
                if t.get("example"):
                    out.append(f'  example: {_arg_call_sig(t)} -> "'
                               f'{t["example"].get("result", "...")}"')
        out += [
            "",
            "TOOL USE - when a registered tool would help, reply with type",
            '"lua" whose "code" is a TOOL PROGRAM: Lua that calls the',
            "registered tools and ends with `return` of a string. The",
            "program is executed on the device IMMEDIATELY (no deploy",
            "step); its result string is given back to you, then give",
            "your final answer.",
            "Every tool takes ONE table argument, e.g. "
            "mean({numbers={3,5,10}}) or temp_convert({value=100,unit=\"c\"}).",
            "Program limits: one complete statement per line, at most 240",
            "bytes per line; variables do NOT persist between lines - keep",
            "the whole program on ONE line ending in `return <string>`;",
            "only the last line's return value is captured.",
            "Never define on_adv/transform/manifest in a tool program -",
            "those are filter/pack artifacts and go through the human",
            "deploy confirmation instead.",
        ]
        return "\n".join(out)


# ---- Bounded rolling device buffer (ingress validation + per-plane caps) ---

class DeviceBuffer:
    """Only lines that parse as JSON enter the buffer; anything else is
    dropped and counted (recent drops stay viewable as diagnostics)."""

    def __init__(self):
        self.adv = deque(maxlen=ADV_BUF_MAX)
        self.conn = deque(maxlen=CONN_BUF_MAX)
        self.console = deque(maxlen=CONSOLE_KEEP)
        self.dropped = 0

    def add_line(self, txt):
        """Classify one raw line; returns 'adv'|'conn'|'resp'|None."""
        if not txt.startswith("{"):
            self.dropped += 1
            self.console.append(txt)
            return None
        try:
            obj = json.loads(txt)
        except (json.JSONDecodeError, ValueError):
            self.dropped += 1
            self.console.append(txt)
            return None
        if not isinstance(obj, dict):
            self.dropped += 1
            return None
        if obj.get("src") == "conn":
            self.conn.append(obj)
            return "conn"
        if "status" in obj:
            return "resp"          # command response, not device data
        if "addr" in obj:
            # an adv line, never a response: CONN STATUS echoes the peer
            # address, so "addr" must be tested AFTER "status" (a CONN
            # STATUS response would otherwise pollute the adv plane)
            self.adv.append(obj)
            return "adv"
        self.dropped += 1
        return None

    def counts(self):
        addrs = len({a.get("addr") for a in self.adv})
        return (f"buffer: {len(self.adv)} adv lines ({addrs} addresses), "
                f"{len(self.conn)} conn lines, {self.dropped} invalid dropped")

    def snapshot(self):
        """Reduced view for the LLM: newest line per address on the adv
        plane (capped), recent conn lines (capped)."""
        if not self.adv and not self.conn:
            return ""
        parts = [self.counts()]
        if self.adv:
            seen = set()
            kept = []
            for a in reversed(self.adv):
                addr = a.get("addr")
                if addr in seen:
                    continue
                seen.add(addr)
                kept.append(a)
                if len(kept) >= SNAPSHOT_ADV_MAX:
                    break
            parts.append("adv lines (one per address):")
            parts += [json.dumps(a, separators=(",", ":")) for a in kept]
        if self.conn:
            parts.append("conn lines (analysis only):")
            parts += [json.dumps(c, separators=(",", ":"))
                      for c in list(self.conn)[-SNAPSHOT_CONN_MAX:]]
        return "\n".join(parts)


class History:
    """Multi-turn conversation, trimmed to a character (token-proxy)
    budget from the oldest end; the current turn is never in it."""

    def __init__(self, char_budget=HISTORY_CHAR_BUDGET):
        self.char_budget = char_budget
        self.turns = []

    def add(self, role, content):
        self.turns.append({"role": role, "content": content})

    def view(self):
        out = list(self.turns)
        total = sum(len(t["content"]) for t in out)
        while out and total > self.char_budget:
            if (len(out) >= 2 and out[0]["role"] == "user"
                    and out[1]["role"] == "assistant"):
                total -= len(out.pop(0)["content"])
                total -= len(out.pop(0)["content"])
            else:
                total -= len(out.pop(0)["content"])
        return out


# ---- Console: non-blocking stdin (R1) with piped-stdin fallback ------------

class ConsoleReader:
    """Polls for a completed input line without ever blocking the Prompt
    loop. Windows console: msvcrt.kbhit/getwch with manual echo; POSIX
    tty: select(); a piped stdin (scripted sessions) is read up front."""

    def __init__(self, tee=None):
        self.tee = tee
        self.interactive = sys.stdin.isatty()
        self.buf = ""
        self.piped = []
        self.piped_i = 0
        if not self.interactive:
            self.piped = [l.rstrip("\r\n") for l in sys.stdin.readlines()]

    def echo(self, text):
        print(text, end="", flush=True)
        if self.tee:
            self.tee.write(text)

    def begin_prompt(self):
        if self.interactive:
            self.echo("you: ")

    def poll_line(self):
        """None: nothing complete yet. '': stdin reached EOF. Else: the
        completed input line (echo already handled here)."""
        if not self.interactive:
            if self.piped_i < len(self.piped):
                line = self.piped[self.piped_i]
                self.piped_i += 1
                return line
            return ""
        if os.name == "nt":
            import msvcrt
            while msvcrt.kbhit():
                ch = msvcrt.getwch()
                if ch == "\x03":
                    raise KeyboardInterrupt
                if ch in ("\r", "\n"):
                    line, self.buf = self.buf, ""
                    self.echo("\n")
                    return line
                if ch in ("\x08", "\x7f"):
                    if self.buf:
                        self.buf = self.buf[:-1]
                        self.echo("\b \b")
                    continue
                self.buf += ch
                self.echo(ch)
            return None
        import select
        ready, _, _ = select.select([sys.stdin], [], [], 0)
        if not ready:
            return None
        line = sys.stdin.readline()
        if line == "":
            return ""
        return line.rstrip("\r\n")


class Tee:
    """Mirror everything the session says to a transcript file (6.7)."""

    def __init__(self, path):
        self.f = open(path, "w", encoding="utf-8", errors="replace")

    def write(self, text):
        self.f.write(text)
        self.f.flush()

    def say(self, msg=""):
        print(msg)
        self.write(msg + "\n")

    def close(self):
        self.f.close()


# ---- Serial helpers (cmd_json/upload patterns copied from llm_loop.py) -----

def describe_resp(r):
    if r is None:
        return "no response (timeout)"
    if r.get("status") == "error" and r.get("code") == -451:
        return "conn not enabled on this firmware"
    return json.dumps(r, separators=(",", ":"))


def expected_cmd(line):
    """Response 'cmd' field for a CLI command line ('CONN TARGET x' ->
    'conn_target'); None when unknown. Readers match responses by this
    so a stale status line from a previous command can never satisfy
    the wrong exchange (the 2026-08-28 stale-line lesson, applied to
    the host tools after the golden-fixture test tripped it)."""
    words = line.split()
    if len(words) >= 2 and words[0] in ("SCAN", "CONN", "SCRIPT", "POWER",
                                        "LUA", "PACK"):
        return (words[0] + "_" + words[1]).lower()
    if words and words[0] in ("STATUS", "VERSION"):
        return words[0].lower()
    return None


def cmd_json(s, line, buf=None, timeout=4.0):
    """Send one CLI command; return the parsed JSON status response.
    Lines seen while waiting are fed to the device buffer. Responses
    are matched by their cmd field to the command issued."""
    want = expected_cmd(line)
    s.reset_input_buffer()
    s.write((line + "\n").encode())
    s.flush()
    end = time.time() + timeout
    while time.time() < end:
        raw = s.readline()
        if not raw:
            continue
        txt = raw.decode(errors="replace").strip()
        if not txt:
            continue
        if buf is not None:
            buf.add_line(txt)
        if txt.startswith("{") and '"status"' in txt:
            try:
                obj = json.loads(txt)
            except (json.JSONDecodeError, ValueError):
                continue
            if isinstance(obj, dict) and "status" in obj and (
                    want is None or obj.get("cmd") == want):
                return obj
    return None


def drain_serial(s, seconds, buf):
    """Continuous drain: classify everything that arrives (2.3 item 1)."""
    if s is None:
        time.sleep(seconds)
        return 0
    s.timeout = DRAIN_TIMEOUT
    end = time.time() + seconds
    n = 0
    while time.time() < end:
        raw = s.readline()
        if not raw:
            continue
        txt = raw.decode(errors="replace").strip()
        if txt:
            buf.add_line(txt)
            n += 1
    return n


def upload_script(s, code, buf):
    """F4.2 text-line bridge upload (SCRIPT LOAD -> paced lines -> END).
    The bridge scans every data line fail-closed and answers a violating
    line IMMEDIATELY with -612 (then resets the session), so the send
    loop watches for that mid-upload response instead of reporting a
    misleading -611 state error from the SCRIPT END that follows."""
    r = cmd_json(s, "SCRIPT LOAD", buf)
    if not r or r.get("status") != "ok":
        return False, describe_resp(r)
    old_timeout = s.timeout
    s.timeout = 0.12
    try:
        for line in code.splitlines():
            s.write((line + "\n").encode())
            s.flush()
            time.sleep(0.05)
            # good data lines are acked silently; any error line arriving
            # here is the bridge rejecting this very line
            while True:
                raw = s.readline()
                if not raw:
                    break
                txt = raw.decode(errors="replace").strip()
                if not txt:
                    continue
                if buf is not None:
                    buf.add_line(txt)
                if txt.startswith("{") and '"status":"error"' in txt:
                    try:
                        return False, describe_resp(json.loads(txt))
                    except (json.JSONDecodeError, ValueError):
                        return False, txt
        time.sleep(0.3)
    finally:
        s.timeout = old_timeout
    r = cmd_json(s, "SCRIPT END", buf)
    if not r or r.get("status") != "ok":
        return False, describe_resp(r)
    return True, "uploaded"


def upload_pack(s, name, src, autorun=False, buf=None):
    """H6.1 M2 pack upload (PACK BEGIN -> paced text lines -> END).
    Same shape as upload_script: good data lines ack silently; the
    fail-closed device scan rejects a violating line IMMEDIATELY with
    -612 (and aborts), so the send loop watches for that instead of a
    misleading -611 from the PACK END that follows."""
    r = cmd_json(s, "PACK BEGIN " + name + (" autorun" if autorun else ""),
                 buf)
    if not r or r.get("status") != "ok":
        return False, describe_resp(r)
    old_timeout = s.timeout
    s.timeout = 0.12
    try:
        for line in src.splitlines():
            ln = line.rstrip()
            if not ln.strip():
                continue
            s.write((ln + "\n").encode())
            s.flush()
            time.sleep(0.05)
            while True:
                raw = s.readline()
                if not raw:
                    break
                txt = raw.decode(errors="replace").strip()
                if not txt:
                    continue
                if buf is not None:
                    buf.add_line(txt)
                if txt.startswith("{") and '"status":"error"' in txt:
                    try:
                        return False, describe_resp(json.loads(txt))
                    except (json.JSONDecodeError, ValueError):
                        return False, txt
        time.sleep(0.3)
    finally:
        s.timeout = old_timeout
    r = cmd_json(s, "PACK END", buf)
    if not r or r.get("status") != "ok":
        return False, describe_resp(r)
    return True, "stored"


def stop_all(s, tee, buf):
    """Orderly cleanup on every exit path (N3: port closes after this).
    Errors here are expected when a plane is already idle; they are
    reported, not hidden."""
    if s is None:
        return
    for cmd in ("SCRIPT STOP", "SCAN STOP", "CONN STOP"):
        r = cmd_json(s, cmd, buf, timeout=2.0)
        name = cmd.lower().replace(" ", "_")
        if r is not None and r.get("status") == "ok":
            tee.say(f"  ({name} ok)")
        else:
            tee.say(f"  ({name}: {describe_resp(r)})")


# ---- Session ----------------------------------------------------------------

HELP_TEXT = """\
commands:
  /samples [n]     show the device buffer snapshot (default 10 lines)
  /scan on|off     start/stop the advertisement plane (SCAN START/STOP)
  /conn on [addr [public|random]]   start conn plane (CONN START)
  /conn off        stop the conn plane (CONN STOP)
  /conn status     connection state and counters (CONN STATUS)
  /conn target <svc-uuid> [<chr-uuid>]   preset the auto-connect target
  /deploy          re-offer the last Lua artifact
  /tools           list registered tool packs (H6.1 tool registry)
  /tools refresh   re-read the manifest from the device
  /tools load <f>  register a local pack file (confirm-gated, run mode)
  /tools load @n   activate a device-stored pack (confirm-gated)
  /tools persist <name> [autorun]  store the active pack on the device
  /history         conversation summary
  /help            this text
  /quit            cleanup and exit (Ctrl+C does the same)
anything else is a prompt for the LLM. Typing while a turn is in flight
is buffered and echoed when the turn finishes."""


class Session:
    def __init__(self, port, no_llm=False, system_file=None,
                 system_extra=None):
        self.port = port
        self.no_llm = no_llm
        self.tee = Tee(time.strftime("assistant_%Y%m%d_%H%M%S.log"))
        self.reader = ConsoleReader(self.tee)
        self.buf = DeviceBuffer()
        self.history = History()
        self.cfg = None if no_llm else resolve_llm_config()
        self.cfg_from_env = bool(
            os.environ.get("LLM_BASE_URL") or os.environ.get("LLM_API_KEY")
            or os.environ.get("OPENAI_API_KEY")
            or os.environ.get("LLM_MODEL"))
        self.system_prompt, self.system_note = compose_system_prompt(
            system_file, system_extra)
        self.last_lua = None
        self.tools = ToolRegistry()
        self.s = None

    def open_device(self, port):
        try:
            self.s = serial.Serial(port, BAUD, timeout=1)
            time.sleep(0.5)
            self.s.reset_input_buffer()
        except serial.SerialException as e:
            self.tee.say(f"(no device on {port}: {e})")
            self.tee.say("(running without live data; "
                         "/scan /conn /deploy are disabled)")
            self.s = None

    # -- startup / teardown --------------------------------------------------

    def banner(self):
        model = self.cfg["model"] if self.cfg else "none (--no-llm or no key)"
        source = ("environment variables (unset LLM_API_KEY to use .llm_env)"
                  if self.cfg and self.cfg_from_env else ".llm_env")
        self.tee.say(f"=== assistant session "
                     f"({time.strftime('%Y-%m-%d %H:%M')}) ===")
        self.tee.say(f"llm backend: {model}  (config: {source})")
        self.tee.say("system prompt: " + self.system_note)
        self.tee.say("transcript: " + os.path.abspath(self.tee.f.name))
        self.open_device(self.port)
        self.tee.say("mode boundary:")
        self.tee.say("  adv lines (addr/type/rssi/...) -> "
                     "Lua artifacts allowed (on_adv/transform hooks)")
        self.tee.say('  conn lines (src:"conn")       -> '
                     "analysis only (they bypass the Lua hooks)")
        if self.s is not None:
            r = cmd_json(self.s, "STATUS", self.buf)
            if r and r.get("status") == "ok":
                state = "scanning" if r.get("scanning") else "idle"
                self.tee.say(f"device: {state}"
                             + ("" if r.get("scanning")
                                else "  (/scan on to start)"))
            drain_serial(self.s, 1.5, self.buf)
            self.tee.say("(" + self.buf.counts() + ")")
        if self.cfg is None:
            reason = ("--no-llm mode: prompts stay local" if self.no_llm
                      else "no LLM key configured: set LLM_API_KEY or "
                           ".llm_env")
            self.tee.say(f"({reason}; free text will not reach any cloud "
                         "service)")
        self.tee.say("type /help for commands; /quit exits")

    def cleanup(self, reason):
        self.tee.say(f"({reason} - cleaning up)")
        try:
            stop_all(self.s, self.tee, self.buf)
        finally:
            if self.s is not None:
                self.s.close()
            self.tee.say("transcript saved: "
                         + os.path.abspath(self.tee.f.name))
            self.tee.close()

    # -- LLM turn ------------------------------------------------------------

    def _try_401_fallback(self, err):
        """An ambient shell key can hijack the config (env-wins-as-unit)
        and 401 against the wrong provider - observed twice on this
        machine. On a 401 with an env-sourced config, switch once to the
        .llm_env file config when it differs; the switch is announced,
        never silent."""
        if "HTTP 401" not in str(err) or not self.cfg_from_env:
            return False
        file_cfg = resolve_llm_config(force_file=True)
        if not file_cfg or file_cfg["key"] == self.cfg["key"]:
            return False
        self.tee.say(f"(llm key from the shell environment was rejected "
                     f"(401) - switching to .llm_env "
                     f"[{file_cfg['model']}])")
        self.cfg = file_cfg
        self.cfg_from_env = False
        return True

    def chat_envelope(self, messages):
        """One LLM round trip with the Rec1 ladder (401 self-heal, then
        ONE escaping-focused retry, fenced-lua extraction as the last
        resort). Returns (envelope | None, last_raw_reply); raises on
        transport errors the self-heals cannot cover."""
        try:
            reply = llm_chat(self.cfg, messages, self.tee.say)
        except RuntimeError as e:
            if not self._try_401_fallback(e):
                raise
            reply = llm_chat(self.cfg, messages, self.tee.say)
        env, fenced = parse_envelope(reply, allow_fenced=False)
        if env is None:
            self.tee.say("(reply was not a valid JSON envelope - "
                         "one retry with an escaping-focused correction)")
            messages.append({"role": "assistant", "content": reply})
            messages.append({"role": "user", "content": ESCAPE_RETRY_MSG})
            reply = llm_chat(self.cfg, messages, self.tee.say)
            env, fenced = parse_envelope(reply)   # fence allowed now:
            # extraction is the last resort, after the one retry (Rec1)
        if env is not None and fenced:
            self.tee.say("(recovered the artifact from a fenced lua block "
                         "after two invalid envelopes)")
        return env, reply

    def ask_llm(self, user_text):
        snapshot = self.buf.snapshot()
        messages = build_messages(self.history.view(), snapshot, user_text,
                                  self.system_prompt + self.tools.prompt())
        self.tee.say(f"(asking {self.cfg['model']} - {self.buf.counts()})")
        # history keeps a compact marker, not the full (stale) snapshot
        self.history.add("user", "Device data snapshot attached. " + user_text)
        executed = 0
        while True:
            env, reply = self.chat_envelope(messages)
            code = env.get("code") if env is not None else None
            is_program = (
                env is not None and env["type"] == "lua"
                and code is not None and not self.tools.is_empty()
                and not DEPLOY_RE.search(strip_outer_fence(code)))
            if is_program and executed < TOOL_EXEC_CAP:
                # generate-and-execute (H6.1 d7+d10): run the program on
                # the device now, hand the result back, let the LLM finish
                ok, result = self.exec_tool_program(strip_outer_fence(code))
                executed += 1
                self.history.add("assistant", "(tool program) " + code[:120])
                if ok:
                    self.tee.say("tool> " + result)
                    feedback = ("Tool program executed on the device. "
                                "Result string:\n" + result)
                    self.history.add("user", "TOOL RESULT: " + result[:200])
                else:
                    self.tee.say("(tool execution failed: " + result + ")")
                    feedback = ("The device refused the tool program:\n"
                                + result
                                + "\nFix the program or answer without "
                                "executing anything.")
                    self.history.add("user", "TOOL ERROR: " + result[:200])
                messages.append({"role": "assistant", "content": reply})
                messages.append({"role": "user", "content": feedback})
                continue
            if is_program:
                # cap reached (d10 loop guard): one corrective round, then
                # the turn must end in a plain answer
                self.tee.say(f"(tool execution cap reached ({TOOL_EXEC_CAP}/"
                             "turn) - asking for a final answer without "
                             "further execution)")
                messages.append({"role": "assistant", "content": reply})
                messages.append({"role": "user", "content":
                                 "Execution budget for this turn is used up. "
                                 "Give your final answer now (type answer); "
                                 "do not reply with type lua."})
                env, reply = self.chat_envelope(messages)
                if (env is not None and env["type"] == "lua"
                        and env.get("code") is not None
                        and not DEPLOY_RE.search(
                            strip_outer_fence(env["code"]))):
                    self.tee.say("(stopped: the LLM kept proposing tool "
                                 "programs after the cap - nothing more was "
                                 "executed)")
                    return
            if env is None:
                preview = reply if len(reply) <= 200 else reply[:200] + "..."
                self.tee.say("llm (error): reply was not a valid typed "
                             f"envelope even after retry. Raw reply: {preview}")
                return
            self.history.add("assistant", json.dumps(env, ensure_ascii=False))
            self.route(env)
            return

    def route(self, env):
        t = env["type"]
        if t == "answer":
            self.tee.say("llm: " + env["text"])
        elif t == "clarify":
            self.tee.say("llm asks: " + env["text"])
            self.tee.say("(answer the question with your next prompt)")
        elif t == "error":
            self.tee.say("llm (error): " + env["text"])
        elif t == "lua":
            code = strip_outer_fence(env["code"])
            self.last_lua = code
            self.tee.say("llm proposes Lua - " + env["text"])
            self.tee.say("--- lua code ---")
            for ln in code.splitlines():
                self.tee.say("  " + ln)
            self.tee.say("--- end ---")
            self.confirm_deploy()

    def confirm_deploy(self):
        self.tee.say("deploy? [y/N]")
        line = self.wait_for_line()
        if line is None:
            self.tee.say("(input closed - artifact kept, not deployed)")
            return
        self.tee.say(f"  confirm> {line}")
        if line.strip().lower() in ("y", "yes"):
            self.deploy_lua()
        else:
            self.tee.say("(kept as the last artifact - /deploy re-offers it)")

    def deploy_lua(self):
        if self.s is None:
            self.tee.say("(no device - cannot deploy)")
            return
        r = cmd_json(self.s, "STATUS", self.buf)
        if not (r and r.get("scanning")):
            r = cmd_json(self.s, "SCAN START", self.buf)
            if not (r and r.get("status") == "ok"):
                self.tee.say("(deploy aborted: SCAN START failed: "
                             + describe_resp(r) + ")")
                return
            self.tee.say("(scan started; collecting 3s baseline)")
            drain_serial(self.s, 3.0, self.buf)
        ok, detail = upload_script(self.s, self.last_lua, self.buf)
        if not ok:
            self.tee.say("(deploy rejected by the device: " + detail + ")")
            self.tee.say("(the session continues; the stream is unchanged)")
            return
        r = cmd_json(self.s, "SCRIPT RUN", self.buf)
        if not (r and r.get("status") == "ok"):
            self.tee.say("(SCRIPT RUN failed: " + describe_resp(r) + ")")
            return
        self.tee.say("(script running - 5s of the filtered stream)")
        n = drain_serial(self.s, 5.0, self.buf)
        self.tee.say(f"({n} lines observed; {self.buf.counts()})")
        self.tee.say("(stop it any time with Ctrl+C or /scan off cleanup)")

    # -- commands ------------------------------------------------------------

    def do_samples(self, arg):
        n = 10
        if arg:
            try:
                n = max(1, int(arg))
            except ValueError:
                self.tee.say("usage: /samples [n]")
                return
        self.tee.say("(" + self.buf.counts() + ")")
        for a in list(self.buf.adv)[-n:]:
            self.tee.say("  adv  " + json.dumps(a, separators=(",", ":")))
        for c in list(self.buf.conn)[-n:]:
            self.tee.say("  conn " + json.dumps(c, separators=(",", ":")))
        for c in list(self.buf.console)[-3:]:
            self.tee.say("  drop " + c[:120])

    def do_scan(self, arg):
        if self.s is None:
            self.tee.say("(no device)")
            return
        if arg not in ("on", "off"):
            self.tee.say("usage: /scan on|off")
            return
        r = cmd_json(self.s, "SCAN START" if arg == "on" else "SCAN STOP",
                     self.buf)
        if not (r and r.get("status") == "ok"):
            self.tee.say("(failed: " + describe_resp(r) + ")")
            return
        if arg == "on":
            self.tee.say("(scan started; collecting 5s of advertisements)")
            drain_serial(self.s, 5.0, self.buf)
            self.tee.say("(" + self.buf.counts() + ")")
        else:
            self.tee.say("(scan stopped)")

    def do_conn(self, args):
        if self.s is None:
            self.tee.say("(no device)")
            return
        parts = args.split()
        if not parts:
            self.tee.say("usage: /conn on [addr [public|random]] | off | "
                         "status | target <svc> [<chr>]")
            return
        sub = parts[0]
        if sub == "on":
            line = "CONN START" + ("" if len(parts) == 1
                                   else " " + " ".join(parts[1:]))
            r = cmd_json(self.s, line, self.buf, timeout=15.0)
            if r is not None and r.get("status") == "ok":
                mode = r.get("mode", "?")
                self.tee.say(f"(conn started, mode {mode}; waiting 3s "
                             "for the first lines)")
                drain_serial(self.s, 3.0, self.buf)
                self.tee.say("(" + self.buf.counts() + ")")
            else:
                # -451 included via describe_resp (feature-off firmware, R2)
                self.tee.say("(conn start failed: " + describe_resp(r) + ")")
        elif sub == "off":
            r = cmd_json(self.s, "CONN STOP", self.buf)
            if r and r.get("status") == "ok":
                self.tee.say("(conn stopped)")
            else:
                self.tee.say("(conn stop: " + describe_resp(r) + ")")
        elif sub == "status":
            r = cmd_json(self.s, "CONN STATUS", self.buf)
            if r and r.get("status") == "ok":
                self.tee.say(f"  state {r.get('state')} peer "
                             f"{r.get('addr')} mode {r.get('mode')} "
                             f"rx_notify {r.get('rx_notify')} "
                             f"rx_read {r.get('rx_read')} "
                             f"errors {r.get('errors')}")
            else:
                self.tee.say("(conn status failed: " + describe_resp(r) + ")")
        elif sub == "target" and len(parts) >= 2:
            r = cmd_json(self.s, "CONN TARGET " + " ".join(parts[1:]),
                         self.buf)
            self.tee.say("(conn target: " + describe_resp(r) + ")")
        else:
            self.tee.say("usage: /conn on [addr [public|random]] | off | "
                         "status | target <svc> [<chr>]")

    def do_history(self):
        view = self.history.view()
        if not view:
            self.tee.say("(no conversation yet)")
            return
        self.tee.say(f"(conversation: {len(view)} turns within the "
                     f"{HISTORY_CHAR_BUDGET}-char budget)")
        for i, t in enumerate(view):
            head = t["content"][:70].replace("\n", " ")
            self.tee.say(f"  {i + 1}. {t['role']}: {head}")

    # -- H6.1 tool registry ---------------------------------------------------

    def lua_exec_result(self, code):
        """One LUA EXEC -> (ok, result_string | error_detail). Self-heals
        the engine-not-initialized case once via LUA INIT."""
        r = cmd_json(self.s, "LUA EXEC " + code, self.buf)
        if (r is not None and r.get("status") == "error"
                and "not initialized" in str(r.get("msg", "")).lower()):
            init = cmd_json(self.s, "LUA INIT", self.buf)
            if init is not None and init.get("status") == "ok":
                r = cmd_json(self.s, "LUA EXEC " + code, self.buf)
        if r is None:
            return False, "no response (timeout)"
        if r.get("status") != "ok":
            return False, describe_resp(r)
        return True, r.get("result", "")

    def fetch_manifest(self):
        """Chunked manifest() fetch: the 256 B LUA EXEC result path cannot
        carry a whole manifest, so string.sub slices are joined host-side.
        Returns (text, None) or (None, error_detail)."""
        parts, pos = [], 1
        while pos <= MANIFEST_MAX:
            ok, chunk = self.lua_exec_result(
                "return string.sub(manifest()," + str(pos) + ","
                + str(pos + MANIFEST_CHUNK - 1) + ")")
            if not ok:
                return None, chunk
            if not chunk:
                break
            parts.append(chunk)
            if len(chunk) < MANIFEST_CHUNK:
                break
            pos += MANIFEST_CHUNK
        else:
            return None, "manifest exceeds the 8 KB budget"
        text = "".join(parts)
        if not text:
            return None, "manifest() returned an empty string"
        return text, None

    def exec_tool_program(self, code):
        """Run-mode execution (decision 14 `run` + decision 10 autonomy):
        validate lines host-side, exec each on the device, the LAST line's
        return value is the program result. Returns (ok, result)."""
        lines, err = lua_exec_lines(code, kind="tool program",
                                    max_lines=TOOL_PROGRAM_MAX_LINES)
        if err:
            return False, err
        if self.s is None:
            return False, "no device connected"
        result = ""
        for ln in lines:
            ok, out = self.lua_exec_result(ln)
            if not ok:
                return False, f"device rejected {ln[:60]!r}: {out}"
            result = out or "(no return value)"
        return True, result

    def device_packs(self):
        """H6.1 M2: PACK LIST -> the persisted packs (or None when the
        device/feature is unavailable)."""
        if self.s is None:
            return None
        r = cmd_json(self.s, "PACK LIST", self.buf)
        if not r or r.get("status") != "ok":
            return None
        return r.get("packs", [])

    def do_tools(self, arg):
        parts = arg.split()
        if not parts:
            if self.tools.is_empty():
                self.tee.say("(no tool packs registered - /tools load "
                             "<file.lua>; the demo lives in "
                             "host_app/tool_packs/demo.lua)")
            else:
                self.tee.say(self.tools.describe())
            stored = self.device_packs()
            if stored:
                marks = []
                for p in stored:
                    active = any(pk["name"] == p.get("name")
                                 for pk in self.tools.packs)
                    marks.append(str(p.get("name"))
                                 + (" (autorun)" if p.get("autorun") else "")
                                 + (" [active]" if active else ""))
                self.tee.say("device packs (persisted): " + ", ".join(marks))
            return
        if parts[0] == "refresh":
            self.do_tools_refresh()
        elif parts[0] == "load" and len(parts) >= 2:
            self.do_tools_load(" ".join(parts[1:]))
        elif parts[0] == "persist" and len(parts) >= 2:
            self.do_tools_persist(" ".join(parts[1:]))
        else:
            self.tee.say("usage: /tools [ | refresh | load <file.lua> | "
                         "load @<device-pack> | persist <name> [autorun] ]")

    def do_tools_refresh(self):
        if self.tools.is_empty():
            self.tee.say("(no packs registered)")
            return
        if self.s is None:
            self.tee.say("(no device)")
            return
        text, err = self.fetch_manifest()
        if err:
            if "global 'manifest'" in err or "attempt to call" in err:
                self.tee.say("(device manifest() is gone - the Lua state "
                             "was probably reset; registry cleared, "
                             "/tools load to re-register)")
                self.tools = ToolRegistry()
            else:
                self.tee.say("(refresh failed: " + err + ")")
            return
        m, verr = validate_manifest(text)
        if verr:
            self.tee.say("(refresh: " + verr + ")")
            return
        for p in self.tools.packs:
            if p["name"] == m["name"]:
                p["tools"] = m["tools"]
                self.tee.say(f"(refreshed pack '{m['name']}': "
                             f"{len(m['tools'])} tools)")
                return
        self.tee.say(f"(device serves pack '{m['name']}' which is not in "
                     "the registry - /tools load it to register)")

    def do_tools_load(self, path):
        if path.startswith("@"):
            # H6.1 M2: activate a device-stored pack (PACK RUN)
            self.do_tools_run_device(path[1:])
            return
        if self.s is None:
            self.tee.say("(no device - packs are registered on the device)")
            return
        cands = ([path] if os.path.isabs(path)
                 else [path, os.path.join(TOOL_PACKS_DIR, path)])
        src = None
        for c in cands:
            if os.path.isfile(c):
                with open(c, encoding="utf-8") as f:
                    src = f.read()
                path = c
                break
        if src is None:
            self.tee.say("(pack file not found: " + path + ")")
            return
        lines, err = lua_exec_lines(src, kind="pack")
        if err:
            self.tee.say("(" + err + ")")
            return
        # AC#2: validate the manifest and reject name collisions HOST-SIDE,
        # before any device line is sent (a rejected pack must leave no
        # side effects on the device - e.g. an on_adv the device keeps).
        mtext, asm_err = assemble_manifest_from_source(src)
        if asm_err is None:
            m, verr = validate_manifest(mtext)
            if verr:
                self.tee.say("(manifest rejected: " + verr + ")")
                return
            if any(p["name"] == m["name"] for p in self.tools.packs):
                self.tee.say(f"(pack '{m['name']}' is already registered)")
                return
            clash = {t["name"] for t in m["tools"]} & self.tools.tool_names()
            if clash:
                self.tee.say("(name collision with an active pack: "
                             + ", ".join(sorted(clash)) + ")")
                return
        # registration is the privileged act (decision 4): confirm-gated
        self.tee.say(f"register pack {path} ({len(lines)} LUA EXEC lines, "
                     "run mode - RAM only, nothing persisted)? [y/N]")
        line = self.wait_for_line()
        if line is None:
            self.tee.say("(input closed - pack not registered)")
            return
        self.tee.say(f"  confirm> {line}")
        if line.strip().lower() not in ("y", "yes"):
            self.tee.say("(aborted - nothing was sent to the device)")
            return
        for i, ln in enumerate(lines, 1):
            ok, detail = self.lua_exec_result(ln)
            if not ok:
                self.tee.say(f"(device rejected line {i}: {detail}; "
                             "pack NOT registered)")
                return
        text, err = self.fetch_manifest()
        if err:
            self.tee.say("(pack functions are on the device but the "
                         "manifest fetch failed: " + err + ")")
            return
        m, verr = validate_manifest(text)
        if verr:
            self.tee.say("(manifest rejected: " + verr + "; functions are "
                         "on the device but NOT registered)")
            return
        pack, cerr = self.tools.add(m, path)
        if cerr:
            self.tee.say("(" + cerr + ")")
            return
        self.tee.say(f"(pack '{m['name']}' registered: {len(m['tools'])} "
                     "tools, run mode)")
        self.tee.say(self.tools.describe())

    def do_tools_run_device(self, name):
        """H6.1 M2: activate a device-stored pack (PACK RUN) and register
        it from the served manifest — the "different PC" flow: tools
        without the local pack file."""
        if self.s is None:
            self.tee.say("(no device)")
            return
        if not IDENT_RE.match(name):
            self.tee.say("(invalid pack name: " + name + ")")
            return
        stored = self.device_packs() or []
        if stored and not any(p.get("name") == name for p in stored):
            self.tee.say("(no such device pack; stored: "
                         + ", ".join(str(p.get("name")) for p in stored)
                         + ")")
            return
        # executing stored code is an explicit act: confirm-gated
        self.tee.say(f"activate stored pack '{name}' on the device? [y/N]")
        line = self.wait_for_line()
        if line is None:
            self.tee.say("(input closed - pack not activated)")
            return
        self.tee.say(f"  confirm> {line}")
        if line.strip().lower() not in ("y", "yes"):
            self.tee.say("(aborted)")
            return
        r = cmd_json(self.s, "PACK RUN " + name, self.buf)
        if not (r and r.get("status") == "ok"):
            self.tee.say("(PACK RUN failed: " + describe_resp(r) + ")")
            return
        text, err = self.fetch_manifest()
        if err:
            self.tee.say("(pack ran but the manifest fetch failed: "
                         + err + ")")
            return
        m, verr = validate_manifest(text)
        if verr:
            self.tee.say("(manifest rejected: " + verr + ")")
            return
        pack, cerr = self.tools.add(m, "device:" + name)
        if cerr:
            self.tee.say("(" + cerr + ")")
            return
        self.tee.say(f"(pack '{m['name']}' registered: {len(m['tools'])} "
                     "tools, from device storage)")
        self.tee.say(self.tools.describe())

    def do_tools_persist(self, args):
        """H6.1 M2: persist an ACTIVE pack to device storage (PACK
        upload; optional autorun = boot-time activation, decision 13's
        seam)."""
        parts = args.split()
        if not parts:
            self.tee.say("usage: /tools persist <name> [autorun]")
            return
        name = parts[0]
        autorun = len(parts) > 1 and parts[1].lower() == "autorun"
        if self.s is None:
            self.tee.say("(no device)")
            return
        pack = next((p for p in self.tools.packs if p["name"] == name),
                    None)
        if pack is None or pack["file"].startswith("device:"):
            self.tee.say(f"(pack '{name}' is not registered from a local "
                         "file this session - /tools load it first)")
            return
        try:
            with open(pack["file"], encoding="utf-8") as f:
                src = f.read()
        except OSError as e:
            self.tee.say(f"(cannot read {pack['file']}: {e})")
            return
        lines, err = lua_exec_lines(src, kind="pack")
        if err:
            self.tee.say("(" + err + ")")
            return
        self.tee.say("persist pack '" + name + "' to device storage"
                     + (" and run it at every boot" if autorun else "")
                     + "? [y/N]")
        line = self.wait_for_line()
        if line is None:
            self.tee.say("(input closed - nothing persisted)")
            return
        self.tee.say(f"  confirm> {line}")
        if line.strip().lower() not in ("y", "yes"):
            self.tee.say("(aborted - nothing was sent)")
            return
        ok, detail = upload_pack(self.s, name, src, autorun, self.buf)
        if not ok:
            self.tee.say("(persist rejected by the device: " + detail + ")")
            return
        stored = self.device_packs() or []
        for p in stored:
            if p.get("name") == name:
                self.tee.say(f"(pack '{name}' stored on the device, "
                             f"{p.get('size', '?')} bytes, autorun "
                             f"{'on' if p.get('autorun') else 'off'})")
                return
        self.tee.say("(pack stored)")

    def handle_line(self, line):
        line = line.strip()
        if not line:
            return
        if not line.startswith("/"):
            if self.cfg is None:
                reason = ("--no-llm mode" if self.no_llm
                          else "no LLM key configured")
                self.tee.say(f"({reason} - the prompt was NOT sent "
                             "anywhere)")
            else:
                try:
                    self.ask_llm(line)
                except RuntimeError as e:
                    self.tee.say(f"llm (error): request failed: {e}")
            return
        cmd, _, arg = line.partition(" ")
        if cmd == "/help":
            for ln in HELP_TEXT.splitlines():
                self.tee.say(ln)
        elif cmd == "/samples":
            self.do_samples(arg.strip())
        elif cmd == "/scan":
            self.do_scan(arg.strip())
        elif cmd == "/conn":
            self.do_conn(arg.strip())
        elif cmd == "/tools":
            self.do_tools(arg.strip())
        elif cmd == "/deploy":
            if self.last_lua:
                self.confirm_deploy()
            else:
                self.tee.say("(no Lua artifact yet - ask for one first)")
        elif cmd == "/history":
            self.do_history()
        elif cmd == "/quit":
            raise SystemExit(0)
        else:
            self.tee.say(f"(unknown command {cmd} - /help lists them)")

    # -- message Prompt loop ---------------------------------------------------

    def wait_for_line(self):
        """Used by the deploy confirmation: keep draining while waiting."""
        while True:
            drain_serial(self.s, 0.05, self.buf)
            line = self.reader.poll_line()
            if line == "":
                return None
            if line is not None:
                return line

    def run(self):
        self.banner()
        self.reader.begin_prompt()
        reason = "exit"
        try:
            while True:
                drain_serial(self.s, 0.05, self.buf)
                line = self.reader.poll_line()
                if line == "":
                    self.tee.say("(stdin closed - quitting)")
                    break
                if line is None:
                    time.sleep(0.01)
                    continue
                if not self.reader.interactive:
                    self.tee.say("you: " + line)
                try:
                    self.handle_line(line)
                except SystemExit:
                    break
                # keep the buffer fresh right after the user acted
                drain_serial(self.s, 0.8, self.buf)
                self.reader.begin_prompt()
        except KeyboardInterrupt:
            reason = "interrupted"
        finally:
            self.cleanup(reason)
        return 0


def main():
    p = argparse.ArgumentParser(
        description="Interactive LLM assistant session for the BLE sniffer "
                    "dongle (spec: harness/01-features/stage5-host/).")
    p.add_argument("port", nargs="?", default="COM12",
                   help="serial port (default COM12)")
    p.add_argument("--no-llm", action="store_true",
                   help="never call the LLM (dry runs without a key)")
    p.add_argument("--system-file", metavar="PATH",
                   help="replace the built-in system prompt with the "
                        "file's content (the typed-envelope router stays "
                        "active; replies that ignore the envelope show "
                        "up as clean errors, never a crash)")
    p.add_argument("--system-extra", metavar="PATH",
                   help="append the file's content to the built-in "
                        "system prompt - steer style/persona/behavior "
                        "without losing the envelope contract")
    args = p.parse_args()
    sys.exit(Session(args.port, args.no_llm, args.system_file,
                     args.system_extra).run())


if __name__ == "__main__":
    main()
