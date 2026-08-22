"""Hardware tests for F2.4 BLE connection — C0 peerless control plane.

Exercises everything that does not need a GATT peripheral peer: the CONN
command family, error codes, the state machine, Ctrl+C recovery, and
scan/conn coexistence bookkeeping. The GATT data path (C1-C6) needs a real
peripheral — on this PC the WinRT GATT-server APIs are unavailable, so a
controlled peer (second ESP32 / phone with nRF Connect) is the manual
follow-up, same boundary as documented for the branch verification.
"""
import serial, time, json

PORT = "COM12"

s = serial.Serial(PORT, 115200, timeout=5)
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

def cmd_during_scan(c, timeout=8):
    """Response reader that survives a streaming adv line flow: adv lines
    carry "rssi", command responses do not (CONN STATUS carries "addr" —
    its peer field — so "addr" cannot be the discriminator)."""
    s.write((c + "\n").encode())
    s.flush()
    end = time.time() + timeout
    while time.time() < end:
        line = s.readline()
        if not line:
            continue
        t = line.decode(errors="replace").strip()
        if not t.startswith("{"):
            continue
        try:
            o = json.loads(t)
        except json.JSONDecodeError:
            continue
        if isinstance(o, dict) and "status" in o and "rssi" not in o:
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

# --- Boot sanity ---------------------------------------------------------
r = cmd("STATUS", 1.5)
check("device sanity (STATUS)", r is not None and r.get("cmd") == "status", str(r))
check("STATUS conn object present", r is not None and
      isinstance(r.get("conn"), dict), str(r))
check("conn enabled", r is not None and r["conn"].get("enabled") is True, str(r))
check("conn state off at boot", r is not None and
      r["conn"].get("state") == "off", str(r))

# --- Error paths before any target ---------------------------------------
r = cmd("CONN STATUS")
check("CONN STATUS off", r and r.get("state") == "off" and r.get("mode") == "none", str(r))

r = cmd("CONN")
check("CONN bare -> syntax error", r and r.get("status") == "error" and
      "invalid syntax" in r.get("msg", ""), str(r))

r = cmd("CONN START")
check("START without target -> -456", r and r.get("status") == "error" and
      r.get("code") == -456, str(r))

r = cmd("CONN TARGET zzzz")
check("TARGET bad uuid -> -450", r and r.get("status") == "error" and
      r.get("code") == -450, str(r))

r = cmd("CONN START 00:11:22:33:44")     # malformed address
check("START bad addr -> -450", r and r.get("status") == "error" and
      r.get("code") == -450, str(r))

r = cmd("CONN INTERVAL 50")
check("INTERVAL below range rejected", r and r.get("status") == "error", str(r))
r = cmd("CONN INTERVAL 500")
check("INTERVAL 500 ok", r and r.get("status") == "ok" and
      r.get("value") == 500, str(r))
r = cmd("CONN INTERVAL 99999")
check("INTERVAL above range rejected", r and r.get("status") == "error", str(r))

r = cmd("CONN STOP")
check("STOP when off -> -453", r and r.get("status") == "error" and
      r.get("code") == -453, str(r))

# --- Auto-connect peer search (unlikely 128-bit UUID: nothing matches) ---
UNLIKELY = "A1B2C3D4-E5F6-0789-0A1B-CCDDEEFF0011"
r = cmd(f"CONN TARGET {UNLIKELY}")
check("TARGET 128-bit uuid ok", r and r.get("status") == "ok", str(r))

r = cmd("CONN START")
check("START auto -> ok mode=auto", r and r.get("status") == "ok" and
      r.get("mode") == "auto", str(r))

r = cmd("CONN STATUS", 1.0)
check("state peer_search while searching", r and
      r.get("state") == "peer_search", str(r))

r = cmd("CONN STOP")
check("STOP from peer_search ok", r and r.get("status") == "ok", str(r))
r = cmd("CONN STATUS")
check("state off after stop", r and r.get("state") == "off", str(r))

# --- Ctrl+C interrupts a peer search -------------------------------------
cmd(f"CONN TARGET {UNLIKELY}")
cmd("CONN START")
time.sleep(0.5)
s.write(b"\x03")
s.flush()
time.sleep(0.8)
s.reset_input_buffer()
r = cmd("CONN STATUS")
check("Ctrl+C aborts peer search", r and r.get("state") == "off", str(r))

# --- Direct connect to an absent peer (bounded by link timeout) ----------
r = cmd("CONN START 00:11:22:33:44:55 public", w=9.0)
check("direct connect unreachable -> -455", r and r.get("status") == "error" and
      r.get("code") == -455, str(r))
r = cmd("CONN STATUS")
check("state off after failed direct connect", r and r.get("state") == "off", str(r))

# --- Coexistence bookkeeping: conn commands during an active scan --------
r = cmd_during_scan("SCAN START")
check("SCAN START ok", r and r.get("status") == "ok", str(r))
r = cmd_during_scan("CONN STATUS")
check("CONN STATUS while scanning", r and r.get("state") == "off", str(r))
r = cmd_during_scan(f"CONN TARGET {UNLIKELY}")
check("TARGET while scanning ok", r and r.get("status") == "ok", str(r))
r = cmd_during_scan("CONN START")
check("auto START while scanning ok (tap path)", r and r.get("status") == "ok", str(r))
r = cmd_during_scan("CONN STOP", timeout=12)
check("STOP while scanning ok", r and r.get("status") == "ok", str(r))
r = cmd_during_scan("SCAN STOP")
check("SCAN STOP ok", r and r.get("status") == "ok", str(r))

# --- Adv stream unaffected by the conn feature ---------------------------
r = cmd_during_scan("SCAN START")
s.reset_input_buffer()
end = time.time() + 4
data = b""
while time.time() < end:
    chunk = s.read(4096)
    if chunk:
        data += chunk
check("advertisements still flow", data.count(b'"addr"') > 0,
      f"{data.count(b'\"addr\"')} lines")
r = cmd_during_scan("SCAN STOP")
check("SCAN STOP after churn", r and r.get("status") == "ok", str(r))

s.close()
print(f"\n{PASS} passed, {FAIL} failed")
exit(1 if FAIL else 0)
