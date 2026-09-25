"""Hardware verification for Stage 4 F4.1 (CLI state machine) +
F4.2 (LLM bridge text-line upload). COM12, 115200."""
import serial, time, json

port = "COM12"
baud = 115200

PASS = 0
FAIL = 0

def send_cmd(s, cmd, wait=0.6):
    """Send a command; return the command's JSON response (or None).

    While scanning, BLE advertisement events are interleaved on the
    console — those carry an "addr" field; command responses carry
    "cmd" or an error "msg" instead. Pick the last non-adv JSON line."""
    s.write((cmd + "\n").encode())
    s.flush()
    time.sleep(wait)
    lines = []
    while s.in_waiting > 0:
        line = s.readline().decode(errors='replace').strip()
        if line.startswith('{'):
            lines.append(line)
    s.reset_input_buffer()
    for line in reversed(lines):
        try:
            obj = json.loads(line)
        except json.JSONDecodeError:
            continue
        if isinstance(obj, dict) and "addr" not in obj:
            return obj
    return None

def send_line(s, line, wait=0.2):
    """Send a raw script line during upload (silent ack expected)."""
    s.write((line + "\n").encode())
    s.flush()
    time.sleep(wait)

def check(name, cond, detail=""):
    global PASS, FAIL
    if cond:
        PASS += 1
        print(f"  PASS  {name}")
    else:
        FAIL += 1
        print(f"  FAIL  {name}  {detail}")

s = serial.Serial(port, baud, timeout=5)
time.sleep(1)
s.reset_input_buffer()

# --- F4.1: VERSION / STATUS / state machine ---
print("=== F4.1 CLI ===")
r = send_cmd(s, "VERSION")
check("VERSION 1.0.0", r and r.get("firmware") == "1.0.0", str(r))

r = send_cmd(s, "STATUS")
check("STATUS has state field", r and r.get("state") == "idle", str(r))

r = send_cmd(s, "SCAN INTERVAL 500")
check("SCAN INTERVAL ok", r and r.get("status") == "ok" and r.get("value") == 500, str(r))
r = send_cmd(s, "SCAN INTERVAL 0")
check("SCAN INTERVAL invalid rejected", r and r.get("status") == "error", str(r))

r = send_cmd(s, "SCAN STOP")
check("SCAN STOP in idle rejected", r and r.get("status") == "error", str(r))

r = send_cmd(s, "SCAN START")
check("SCAN START ok", r and r.get("status") == "ok", str(r))
r = send_cmd(s, "SCAN START")
check("SCAN START while scanning rejected", r and r.get("status") == "error", str(r))
r = send_cmd(s, "FILTER ADD NAME test*")
check("FILTER ADD while scanning rejected", r and r.get("status") == "error", str(r))
r = send_cmd(s, "SCAN STOP")
check("SCAN STOP ok", r and r.get("status") == "ok", str(r))

r = send_cmd(s, "FILTER ADD NAME Sensor*")
check("FILTER ADD ok", r and r.get("status") == "ok" and r.get("index") == 0, str(r))
r = send_cmd(s, "FILTER ADD RSSI -70")
check("FILTER ADD RSSI ok", r and r.get("status") == "ok" and r.get("index") == 1, str(r))
r = send_cmd(s, "FILTER LIST")
check("FILTER LIST shows 2", r and r.get("count") == 2 and len(r.get("filters", [])) == 2, str(r))
r = send_cmd(s, "FILTER CLEAR")
check("FILTER CLEAR ok", r and r.get("status") == "ok", str(r))

r = send_cmd(s, "BLAH")
check("unknown command rejected", r and r.get("status") == "error" and "unknown" in r.get("msg", ""), str(r))
r = send_cmd(s, "FILTER ADD")
check("syntax error reports hint", r and "invalid syntax" in r.get("msg", ""), str(r))

# --- F4.2: text-line upload protocol ---
print("=== F4.2 LLM bridge ===")
r = send_cmd(s, "SCRIPT LOAD")
check("SCRIPT LOAD ready", r and r.get("status") == "ok" and r.get("msg") == "ready", str(r))

script = [
    "local count = 0",
    "function on_adv(addr, rssi, name)",
    "    count = count + 1",
    "    return name ~= nil and name ~= ''",
    "end",
    "function transform(json_str)",
    "    return json_str",
    "end",
]
for line in script:
    send_line(s, line)

r = send_cmd(s, "SCRIPT END")
expected_size = sum(len(l) + 1 for l in script)
check("SCRIPT END ok with size", r and r.get("status") == "ok" and r.get("size") == expected_size,
      f"expected size {expected_size}, got {r}")

# Run it (state machine: RUN requires SCANNING)
r = send_cmd(s, "SCRIPT RUN")
check("SCRIPT RUN in idle rejected", r and r.get("status") == "error", str(r))
send_cmd(s, "SCAN START")
r = send_cmd(s, "SCRIPT RUN")
check("SCRIPT RUN while scanning ok", r and r.get("status") == "ok", str(r))
r = send_cmd(s, "STATUS")
check("STATUS script_running", r and r.get("script_running") is True and r.get("state") == "script_running", str(r))
r = send_cmd(s, "SCAN STOP")
check("SCAN STOP while script running rejected", r and r.get("status") == "error", str(r))
r = send_cmd(s, "SCRIPT STOP")
check("SCRIPT STOP ok", r and r.get("status") == "ok", str(r))
send_cmd(s, "SCAN STOP")

# Sandbox violation (AC #7)
r = send_cmd(s, "SCRIPT LOAD")
check("SCRIPT LOAD (violation test)", r and r.get("status") == "ok", str(r))
send_line(s, 'os.execute("ls")')
time.sleep(0.5)
# After a violation the upload is aborted — the next line is parsed as a
# command again; END must report no upload in progress.
r = send_cmd(s, "SCRIPT END")
check("sandbox violation aborts upload", r and r.get("status") == "error", str(r))

# Violation detected synchronously: LOAD, bad line, then LOAD again fails?
r = send_cmd(s, "SCRIPT LOAD")
check("SCRIPT LOAD after abort ok", r and r.get("status") == "ok", str(r))
send_line(s, "local ok_line = 1")
r = send_cmd(s, "SCRIPT END")
check("clean line then END ok", r and r.get("status") == "ok", str(r))

# Empty script
send_cmd(s, "SCRIPT LOAD")
r = send_cmd(s, "SCRIPT END")
check("empty script rejected", r and r.get("status") == "error" and "empty" in r.get("msg", ""), str(r))

# Upload interruption (AC #4): LOAD, partial, then SCAN START proceeds
send_cmd(s, "SCRIPT LOAD")
send_line(s, "local half = true")
r = send_cmd(s, "SCAN START")
check("command aborts upload and proceeds", r and r.get("status") == "ok", str(r))
r = send_cmd(s, "SCRIPT END")
check("END after abort errors", r and r.get("status") == "error", str(r))
send_cmd(s, "SCAN STOP")

# Hex-chunk path still works (regression)
r = send_cmd(s, "SCRIPT BEGIN")
check("SCRIPT BEGIN (hex path) ok", r and r.get("status") == "ok", str(r))
hex_data = "return 1".encode().hex()
r = send_cmd(s, f"SCRIPT CHUNK {hex_data}")
check("SCRIPT CHUNK ok", r and r.get("status") == "ok", str(r))
r = send_cmd(s, "SCRIPT END")
check("SCRIPT END (hex path) ok", r and r.get("status") == "ok", str(r))

s.close()
print(f"\n{PASS} passed, {FAIL} failed")
exit(1 if FAIL else 0)
