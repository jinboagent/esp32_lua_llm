import serial, time, json

s = serial.Serial('COM12', 115200, timeout=5)
time.sleep(1)
s.reset_input_buffer()

def send(cmd, wait=2):
    s.write((cmd + "\n").encode())
    s.flush()
    time.sleep(wait)
    out = []
    while s.in_waiting > 0:
        line = s.readline().decode(errors='replace').strip()
        if line:
            out.append(line)
    s.reset_input_buffer()
    return out

# Upload script: only pass devices with names
script = 'function on_adv(addr, rssi, name)\n  if name then return true end\n  return false\nend'

print("=== Upload filter script ===")
send("SCRIPT BEGIN")
send("SCRIPT CHUNK " + script.encode().hex())
r = send("SCRIPT END", wait=3)
for l in r: print(l)

print("\n=== Run script ===")
r = send("SCRIPT RUN", wait=3)
for l in r: print(l)

print("\n=== Start scan (3 seconds) ===")
send("SCAN START", wait=0.5)
time.sleep(3)
lines = []
while s.in_waiting > 0:
    line = s.readline().decode(errors='replace').strip()
    if line.startswith('{'):
        lines.append(line)
s.reset_input_buffer()

named = 0
unnamed = 0
for line in lines:
    try:
        j = json.loads(line)
        if j.get("name"):
            named += 1
            print(f"  [PASS] name={j['name']}, rssi={j.get('rssi')}")
        else:
            unnamed += 1
    except:
        pass

print(f"\nTotal: {len(lines)} ads, {named} named (passed), {unnamed} unnamed (should be 0 if hook works)")

print("\n=== Cleanup ===")
send("SCAN STOP", wait=0.5)
s.reset_input_buffer()
r = send("SCRIPT STOP")
for l in r: print(l)

s.close()
print("Done.")
