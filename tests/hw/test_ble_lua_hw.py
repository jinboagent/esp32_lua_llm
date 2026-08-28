"""BLE + Lua data-plane hardware suite (v1.0.0 gap coverage).

Device: ESP32-S3 dongle on COM12 (USB-Serial/JTAG). Requires: pyserial.
The port stays OPEN for the whole run — closing it resets the chip
(N3, bug_check 2026-08-10).

What v1.0.0 suites did NOT cover (and this one does):
  L1  VERSION/STATUS schema incl. reset_reason
  L2  ambient capture: JSON schema, ts monotonicity, rssi/type sanity,
      dedup-window invariant (same addr never twice within 1 s)
  L3  Lua sandbox on target: blocked libs nil, math/string/table work,
      -612 compile error, -614 instruction timeout
  L4  on_adv ABI proof: firmware passes 7 args (introspected on device)
  L5  on_adv suppression visible on the USB stream (suppress-all => 0 lines)
  L6  legacy 3-arg hook scripts pass everything (proves the stale
      test_hooks.py / test_hooks2.py verdicts are untrustworthy)
  L7  transform hook rewrites the outgoing JSON line
  L8  SCRIPT STOP restores the default stream
  L9  overlong command line (>255 chars) does not wedge the console
  L10 Ctrl+C (single 0x03) stops the stream immediately

Usage:  python test_ble_lua_hw.py [COM12]
"""
import json
import re
import sys
import time

import serial

PORT = sys.argv[1] if len(sys.argv) > 1 else "COM12"
BAUD = 115200

PASS = 0
FAIL = 0
ADDR_RE = re.compile(r"^([0-9A-F]{2}:){5}[0-9A-F]{2}$")


def check(name, cond, detail=""):
    global PASS, FAIL
    if cond:
        PASS += 1
        print(f"  [PASS] {name}")
    else:
        FAIL += 1
        print(f"  [FAIL] {name}  {detail}")


def cmd_json(s, line, timeout=4.0):
    """Send one command line; return (parsed JSON with 'status', all lines)."""
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
                return json.loads(txt), lines
            except json.JSONDecodeError:
                pass
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
    return advs, console


def upload_lines(s, script):
    """Upload a script via the F4.2 text-line bridge (SCRIPT LOAD)."""
    r, _ = cmd_json(s, "SCRIPT LOAD")
    if not r or r.get("status") != "ok":
        return False, f"LOAD: {r}"
    for line in script.splitlines():
        s.write((line + "\n").encode())
        s.flush()
        time.sleep(0.05)
    time.sleep(0.3)
    r, _ = cmd_json(s, "SCRIPT END")
    return bool(r and r.get("status") == "ok"), str(r)


s = serial.Serial(PORT, BAUD, timeout=1)
time.sleep(0.5)
s.reset_input_buffer()

print(f"=== BLE + Lua data-plane suite on {PORT} ===\n")

# ---- L1 sanity -----------------------------------------------------------
print("[L1] sanity")
r, _ = cmd_json(s, "VERSION")
check("VERSION is 1.0.0", r and r.get("firmware") == "1.0.0", str(r))
r, _ = cmd_json(s, "STATUS")
need = {"state", "reset_reason", "scanning", "queue_drops", "lua_ready",
        "script_loaded", "script_running", "free_storage", "pipeline"}
check("STATUS schema complete", r and need.issubset(r.keys()), str(r))

# ---- L2 baseline capture -------------------------------------------------
print("[L2] baseline ambient capture (8 s)")
r, _ = cmd_json(s, "SCAN START")
check("SCAN START ok", r and r.get("status") == "ok", str(r))
advs, console = collect_adv(s, 8)
check("advertisements received", len(advs) > 0, "no ambient BLE traffic?")

ok_schema = all(("addr" in a and "rssi" in a and "ts" in a and "type" in a)
                for a in advs) if advs else False
check("JSON fields addr/rssi/ts/type present", ok_schema)
ok_addr = all(ADDR_RE.match(a.get("addr", "")) for a in advs) if advs else False
check("addr format AA:BB:CC:DD:EE:FF", ok_addr)
ok_type = all(a.get("type") in ("public", "random") for a in advs) if advs else False
check("addr_type is public|random", ok_type)
ok_rssi = all(-127 <= a.get("rssi", 99) <= 20 for a in advs) if advs else False
check("rssi in plausible range", ok_rssi)

ts_list = [a.get("ts", 0) for a in advs]
check("ts monotonically non-decreasing",
      ts_list == sorted(ts_list) and len(ts_list) > 0, str(ts_list[:5]))

# dedup window invariant: same address at most once per 1000 ms window
dedup_ok = True
worst = None
last_seen = {}
for a in advs:
    t = a.get("ts", 0)
    addr = a.get("addr")
    if addr in last_seen:
        gap = t - last_seen[addr]
        if gap < 950:  # 50 ms tolerance on ts quantization/queueing
            dedup_ok = False
            worst = (addr, gap)
    last_seen[addr] = t
check("dedup window holds (>=~1 s between repeats)", dedup_ok, str(worst))

# ---- L3 Lua sandbox on target --------------------------------------------
print("[L3] Lua sandbox on target")
for lib in ("os", "io", "debug", "package"):
    r, _ = cmd_json(s, f"LUA EXEC return {lib}")
    nil_ok = r and r.get("status") == "ok" and r.get("result", "") == ""
    check(f"{lib} is nil in sandbox", nil_ok, str(r))
r, _ = cmd_json(s, "LUA EXEC return load")
check("load removed", r and r.get("result", "") == "", str(r))
r, _ = cmd_json(s, 'LUA EXEC return string.upper("ok")')
check("string lib works", r and r.get("result") == "OK", str(r))
r, _ = cmd_json(s, 'LUA EXEC return table.concat({"a","b"},"-")')
check("table lib works", r and r.get("result") == "a-b", str(r))
r, _ = cmd_json(s, "LUA EXEC return math.floor(3.9)")
check("math lib works", r and r.get("result") == "3", str(r))
# Syntax error: capture the RAW line. Lua messages embed double quotes
# ('[string "..."]'), so the response is only usable if the firmware
# escapes them. Known-open finding H1 (bug_check 2026-08-11).
s.reset_input_buffer()
s.write(b"LUA EXEC return +++\n")
s.flush()
time.sleep(1.5)
raw_syntax = [l.decode(errors="replace").strip()
              for l in s.readlines() if l.strip()]
json_lines = [l for l in raw_syntax if l.startswith("{") and "lua_exec" in l]
syntax_parsed = None
if json_lines:
    try:
        syntax_parsed = json.loads(json_lines[0])
    except json.JSONDecodeError:
        syntax_parsed = None
check("syntax error -> -612 with VALID JSON response",
      syntax_parsed is not None and syntax_parsed.get("code") == -612,
      f"raw: {json_lines[:1]}")
r, _ = cmd_json(s, "LUA EXEC while true do end", timeout=6)
check("infinite loop -> -614 timeout", r and r.get("code") == -614, str(r))

# ---- L4 on_adv ABI proof (7 args, introspected on device) ----------------
print("[L4] on_adv ABI (firmware must pass 7 args)")
introspect = (
    "captured_n = 0\n"
    "captured_types = ''\n"
    "function on_adv(...)\n"
    "  captured_n = select('#', ...)\n"
    "  local t = {}\n"
    "  for i = 1, captured_n do t[i] = type(select(i, ...)) end\n"
    "  captured_types = table.concat(t, ',')\n"
    "  return true\n"
    "end"
)
ok, detail = upload_lines(s, introspect)
check("upload introspection script", ok, detail)
r, _ = cmd_json(s, "SCRIPT RUN")
check("SCRIPT RUN while scanning", r and r.get("status") == "ok", str(r))
time.sleep(2)  # let at least one adv hit the hook
r, _ = cmd_json(s, "LUA EXEC return captured_n")
check("on_adv receives 7 arguments", r and r.get("result") == "7", str(r))
r, _ = cmd_json(s, "LUA EXEC return captured_types")
types_ok = (r and r.get("result", "").startswith("string,number,number"))
check("arg types start string,number,number", types_ok, str(r))
r, _ = cmd_json(s, "SCRIPT STOP")
check("SCRIPT STOP ok", r and r.get("status") == "ok", str(r))

# ---- L5 on_adv suppression on the stream ----------------------------------
print("[L5] suppress-all hook -> zero adv lines")
suppress = (
    "function on_adv(addr, addr_type, rssi, name, uuids, manu_id, manu_data)\n"
    "  return false\n"
    "end"
)
ok, detail = upload_lines(s, suppress)
check("upload suppress-all script", ok, detail)
r, _ = cmd_json(s, "SCRIPT RUN")
check("SCRIPT RUN ok", r and r.get("status") == "ok", str(r))
time.sleep(1)  # drain queue
advs, _ = collect_adv(s, 6)
check("stream fully suppressed (0 lines)", len(advs) == 0,
      f"{len(advs)} lines leaked")

# ---- L8 SCRIPT STOP restores stream ---------------------------------------
print("[L8] SCRIPT STOP restores default stream")
r, _ = cmd_json(s, "SCRIPT STOP")
check("SCRIPT STOP ok", r and r.get("status") == "ok", str(r))
advs, _ = collect_adv(s, 6)
check("stream resumes after stop", len(advs) > 0, "still silent")

# ---- L6 legacy 3-arg signature passes everything --------------------------
print("[L6] legacy 3-arg script (stale test_hooks.py behavior)")
legacy = (
    "function on_adv(addr, rssi, name)\n"
    "  if name then return true end\n"
    "  return false\n"
    "end"
)
ok, detail = upload_lines(s, legacy)
check("upload legacy 3-arg script", ok, detail)
r, _ = cmd_json(s, "SCRIPT RUN")
check("SCRIPT RUN ok", r and r.get("status") == "ok", str(r))
advs, _ = collect_adv(s, 6)
unnamed = [a for a in advs if not a.get("name")]
check("legacy script leaks unnamed devices (intent was to filter them)",
      len(advs) > 0 and len(unnamed) > 0,
      f"{len(advs)} lines, {len(unnamed)} unnamed")
r, _ = cmd_json(s, "SCRIPT STOP")

# ---- L7 transform hook rewrites JSON ---------------------------------------
print("[L7] transform hook rewrites outgoing JSON")
transform = (
    "function transform(addr, json)\n"
    "  return string.sub(json, 1, #json - 1) .. ',\"hooked\":1}'\n"
    "end"
)
ok, detail = upload_lines(s, transform)
check("upload transform script", ok, detail)
r, _ = cmd_json(s, "SCRIPT RUN")
check("SCRIPT RUN ok", r and r.get("status") == "ok", str(r))
advs, _ = collect_adv(s, 6)
check("transform output received", len(advs) > 0, "no lines")
check("every line carries hooked marker",
      len(advs) > 0 and all(a.get("hooked") == 1 for a in advs),
      f"{sum(1 for a in advs if a.get('hooked') == 1)}/{len(advs)}")
r, _ = cmd_json(s, "SCRIPT STOP")
check("SCRIPT STOP ok", r and r.get("status") == "ok", str(r))
advs, _ = collect_adv(s, 3)
check("marker gone after stop",
      len(advs) > 0 and all("hooked" not in a for a in advs),
      f"{len(advs)} lines")

# ---- L9 overlong command line ----------------------------------------------
print("[L9] overlong line robustness (USB RX buffer = 256)")
s.reset_input_buffer()
s.write(b"X" * 300 + b"\n")
s.flush()
time.sleep(1.5)
leftover = []
while s.in_waiting:
    txt = s.readline().decode(errors="replace").strip()
    if txt:
        leftover.append(txt)
r, _ = cmd_json(s, "STATUS")
check("device still answers after 300-char line", r and r.get("status") == "ok")
tail_cmd = any('"cmd"' in t or "unknown command" in t for t in leftover)
check("INFO: line tail re-parsed as command (expected -504 drain-to-EOL)",
      not tail_cmd, f"leftover lines: {leftover[:3]}")

# ---- L10 Ctrl+C interrupt ---------------------------------------------------
print("[L10] Ctrl+C single-byte interrupt")
r, _ = cmd_json(s, "SCAN START")
if not (r and r.get("status") == "ok"):
    # scan may already run; tolerate 'already scanning'
    check("scan active for interrupt test",
          r and "already" in str(r.get("msg", "")), str(r))
time.sleep(1)
s.write(b"\x03")
s.flush()
time.sleep(0.5)
s.reset_input_buffer()  # drop lines buffered BEFORE the interrupt landed
advs, _ = collect_adv(s, 3)
check("stream stops after 0x03", len(advs) == 0, f"{len(advs)} lines after ^C")
r, _ = cmd_json(s, "STATUS")
check("state idle after interrupt", r and r.get("state") == "idle", str(r))

# ---- cleanup -----------------------------------------------------------------
cmd_json(s, "SCRIPT STOP")
cmd_json(s, "SCAN STOP")
r, _ = cmd_json(s, "STATUS")
check("final STATUS ok", r and r.get("status") == "ok", str(r))

s.close()
print(f"\n{PASS} passed, {FAIL} failed")
sys.exit(1 if FAIL else 0)
