"""Hardware smoke test for F4.3 power management (COM12)."""
import serial, time, json

s = serial.Serial("COM12", 115200, timeout=5)
time.sleep(1)
s.reset_input_buffer()

def cmd(c, w=0.6):
    s.write((c + "\n").encode())
    s.flush()
    time.sleep(w)
    out = [l.decode(errors="replace").strip() for l in s.readlines()
           if l.decode(errors="replace").strip().startswith("{")]
    s.reset_input_buffer()
    for l in reversed(out):
        try:
            o = json.loads(l)
        except json.JSONDecodeError:
            continue
        if isinstance(o, dict) and "status" in o:
            return o
    return None

PASS = 0
FAIL = 0
def check(name, cond, detail=""):
    global PASS, FAIL
    if cond:
        PASS += 1
        print(f"  PASS  {name}")
    else:
        FAIL += 1
        print(f"  FAIL  {name}  {detail}")

r = cmd("POWER STATUS")
check("POWER STATUS sleep_enabled default", r and r.get("sleep_enabled") is True, str(r))
check("POWER STATUS state light_sleep when idle", r and r.get("state") == "light_sleep", str(r))
check("POWER STATUS est_current_ma idle", r and r.get("est_current_ma") == 8, str(r))

r = cmd("POWER SLEEP OFF")
check("POWER SLEEP OFF ok", r and r.get("status") == "ok" and r.get("enabled") is False, str(r))
r = cmd("POWER STATUS")
check("state active when sleep off", r and r.get("state") == "active", str(r))
check("est_current_ma active idle", r and r.get("est_current_ma") == 30, str(r))

r = cmd("POWER SLEEP ON")
check("POWER SLEEP ON ok", r and r.get("status") == "ok" and r.get("enabled") is True, str(r))

# Wake-from-light-sleep: leave the device idle past the idle window,
# then confirm a command still gets an answer (USB activity wakes it).
time.sleep(6)
r = cmd("STATUS", 1.5)
check("wake-on-command after idle", r and r.get("cmd") == "status", str(r))

# Scanning regression with PM enabled: adv events must still flow.
# Capture immediately after SCAN START (dedup suppresses repeats after
# the initial burst), and filter responses line-by-line — readlines()
# blocks while advs stream.
def cmd_during_scan(c, timeout=8):
    s.write((c + "\n").encode())
    s.flush()
    end = time.time() + timeout
    while time.time() < end:
        line = s.readline()
        if not line:
            continue
        t = line.decode(errors="replace").strip()
        if t.startswith("{"):
            try:
                o = json.loads(t)
            except json.JSONDecodeError:
                continue
            if isinstance(o, dict) and "status" in o and "addr" not in o:
                return o
    return None

r = cmd_during_scan("SCAN START")
check("SCAN START with PM", r and r.get("status") == "ok", str(r))
s.reset_input_buffer()
end = time.time() + 4
data = b""
while time.time() < end:
    chunk = s.read(4096)
    if chunk:
        data += chunk
n_advs = data.count(b'"addr"')
check("advertisements still flow", n_advs > 0, f"seen {n_advs}")
r = cmd_during_scan("POWER STATUS")
check("POWER STATUS while scanning", r and r.get("state") == "active"
      and r.get("est_current_ma") == 45, str(r))
r = cmd_during_scan("SCAN STOP")
check("SCAN STOP with PM", r and r.get("status") == "ok", str(r))

r = cmd("POWER NAP")
check("POWER bad arg rejected", r and r.get("status") == "error", str(r))
r = cmd("POWER")
check("POWER bare rejected", r and r.get("status") == "error", str(r))

s.close()
print(f"\n{PASS} passed, {FAIL} failed")
exit(1 if FAIL else 0)
