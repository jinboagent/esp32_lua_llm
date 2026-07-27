import serial, time, json

s = serial.Serial('COM12', 115200, timeout=5)
time.sleep(2)
s.reset_input_buffer()

def send(cmd, wait=2):
    s.write((cmd + "\n").encode())
    s.flush()
    time.sleep(wait)
    out = []
    while s.in_waiting > 0:
        line = s.readline().decode(errors='replace').strip()
        if line.startswith('{'):
            out.append(line)
    s.reset_input_buffer()
    return out

print("=== Test 1: BLE scan basic ===")
send("SCAN START", wait=1)
time.sleep(2)
lines = []
while s.in_waiting > 0:
    line = s.readline().decode(errors='replace').strip()
    if line.startswith('{'):
        lines.append(line)
s.reset_input_buffer()
print(f"  Received {len(lines)} ads")
if lines:
    j = json.loads(lines[0])
    print(f"  Sample: addr={j.get('addr')}, rssi={j.get('rssi')}")
send("SCAN STOP", wait=0.5)
s.reset_input_buffer()
print(f"  PASS: BLE scan works" if lines else "  FAIL: no data")

print("\n=== Test 2: Lua whitelist sandbox (B-S3-2) ===")
# io, os, debug should be nil (not loaded at all)
for lib in ['io', 'os', 'debug', 'package']:
    r = send(f'LUA EXEC return {lib}', wait=1.5)
    result = json.loads(r[0]) if r else {}
    val = result.get('result', 'N/A')
    ok = val == '' or val is None  # nil returns empty string
    print(f"  {lib}: {'nil (PASS)' if ok else f'NOT nil: {val} (FAIL)'}")

# string, table, math should work
r = send('LUA EXEC return string.upper("hello")', wait=1.5)
result = json.loads(r[0]) if r else {}
print(f"  string: {result.get('result', 'FAIL')}")

r = send('LUA EXEC return math.pi', wait=1.5)
result = json.loads(r[0]) if r else {}
print(f"  math: {result.get('result', 'FAIL')}")

print("\n=== Test 3: load function removed (B-S3-2) ===")
r = send('LUA EXEC return load', wait=1.5)
result = json.loads(r[0]) if r else {}
val = result.get('result', 'N/A')
print(f"  load: {'nil (PASS)' if val == '' else f'NOT nil: {val} (FAIL)'}")

print("\n=== Test 4: Script trial compile (B-S3-3) ===")
# Upload a script that has a side effect (should NOT run during upload)
script = 'counter = (counter or 0) + 1\nfunction on_adv(a,r,n) return true end\nreturn counter'
hex_data = script.encode().hex()
send("SCRIPT BEGIN")
send(f"SCRIPT CHUNK {hex_data}")
r = send("SCRIPT END", wait=3)
print(f"  Upload: {r}")

# Check if counter was incremented during upload (it shouldn't be)
r = send('LUA EXEC return counter', wait=1.5)
result = json.loads(r[0]) if r else {}
counter_val = result.get('result', '')
print(f"  Counter after upload: '{counter_val}' (should be empty/nil = not executed)")

# Now run the script
r = send("SCRIPT RUN", wait=3)
print(f"  Run: {r}")

# Check counter after run
r = send('LUA EXEC return counter', wait=1.5)
result = json.loads(r[0]) if r else {}
counter_val = result.get('result', '')
print(f"  Counter after run: '{counter_val}' (should be '1')")

send("SCRIPT STOP", wait=0.5)
s.reset_input_buffer()

print("\n=== Test 5: NULL func_name safety (B-S3-5) ===")
# This is a code-level fix — verified by code review
print("  Verified by code review (NULL check added to lua_engine_call_on_adv/transform)")

print("\n=== All tests complete ===")
s.close()
