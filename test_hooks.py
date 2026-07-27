import serial, time, json

port = "COM12"
baud = 115200
CHUNK_SIZE = 200

def send_cmd(s, cmd, wait=1.5):
    s.write((cmd + "\n").encode())
    s.flush()
    time.sleep(wait)
    lines = []
    while s.in_waiting > 0:
        line = s.readline().decode(errors='replace').strip()
        if line.startswith('{'):
            lines.append(line)
    s.reset_input_buffer()
    if lines:
        try:
            return json.loads(lines[-1])
        except json.JSONDecodeError:
            return {"raw": lines[-1]}
    return None

def upload_script(s, script_text):
    r = send_cmd(s, "SCRIPT BEGIN")
    print(f"  BEGIN: {r}")
    if not r or r.get("status") != "ok":
        return False

    data = script_text.encode()
    offset = 0
    chunk_num = 0
    while offset < len(data):
        chunk = data[offset:offset + CHUNK_SIZE]
        hex_str = chunk.hex()
        r = send_cmd(s, f"SCRIPT CHUNK {hex_str}")
        chunk_num += 1
        print(f"  CHUNK {chunk_num} ({len(chunk)} bytes): {r}")
        if not r or r.get("status") != "ok":
            return False
        offset += len(chunk)

    r = send_cmd(s, "SCRIPT END")
    print(f"  END: {r}")
    return r and r.get("status") == "ok"

# Connect
s = serial.Serial(port, baud, timeout=5)
time.sleep(1)
s.reset_input_buffer()

# Script: pass only named devices
script = 'function on_adv(addr, rssi, name)\n  if name then return true end\n  return false\nend\n'

print("=== Upload script ===")
ok = upload_script(s, script)
print(f"  Upload: {'OK' if ok else 'FAILED'}")

if ok:
    print("\n=== Run script ===")
    r = send_cmd(s, "SCRIPT RUN")
    print(f"  RUN: {r}")

    print("\n=== Script status ===")
    r = send_cmd(s, "SCRIPT STATUS")
    print(f"  STATUS: {r}")

    print("\n=== Scan (5s, should only show named devices) ===")
    send_cmd(s, "SCAN START", wait=0.5)
    time.sleep(5)
    # Read scan output
    lines = []
    while s.in_waiting > 0:
        line = s.readline().decode(errors='replace').strip()
        if line.startswith('{'):
            lines.append(line)
    s.reset_input_buffer()
    print(f"  Received {len(lines)} advertisements")
    for line in lines[:5]:
        try:
            j = json.loads(line)
            name = j.get("name", "(none)")
            rssi = j.get("rssi", "?")
            print(f"    name={name}, rssi={rssi}")
        except:
            print(f"    (raw) {line[:80]}")

    print("\n=== Stop scan and script ===")
    send_cmd(s, "SCAN STOP", wait=0.5)
    s.reset_input_buffer()
    r = send_cmd(s, "SCRIPT STOP")
    print(f"  STOP: {r}")

s.close()
print("\nDone.")
