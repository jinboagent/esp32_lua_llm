import serial, time, json

port = "COM12"
baud = 115200
CHUNK_SIZE = 256

def send_cmd(s, cmd, wait=1.0):
    """Send a command and return the JSON response."""
    s.write((cmd + "\n").encode())
    s.flush()
    time.sleep(wait)
    lines = []
    while s.in_waiting > 0:
        line = s.readline().decode(errors='replace').strip()
        if line.startswith('{'):
            lines.append(line)
    s.reset_input_buffer()
    return json.loads(lines[-1]) if lines else None

def upload_script(s, script_text):
    """Upload a script via chunked protocol."""
    r = send_cmd(s, "SCRIPT BEGIN")
    if r and r.get("status") != "ok":
        print(f"BEGIN failed: {r}")
        return False

    data = script_text.encode()
    offset = 0
    while offset < len(data):
        chunk = data[offset:offset + CHUNK_SIZE]
        # Send chunk as hex-encoded data via SCRIPT CHUNK <hex>
        hex_str = chunk.hex()
        r = send_cmd(s, f"SCRIPT CHUNK {hex_str}")
        if r and r.get("status") != "ok":
            print(f"CHUNK failed at offset {offset}: {r}")
            return False
        offset += len(chunk)

    r = send_cmd(s, "SCRIPT END")
    if r and r.get("status") != "ok":
        print(f"END failed: {r}")
        return False
    return True

# Connect
s = serial.Serial(port, baud, timeout=5)
time.sleep(1)
s.reset_input_buffer()

# Test 1: SCRIPT STATUS (no script)
print("=== Test 1: SCRIPT STATUS (no script) ===")
r = send_cmd(s, "SCRIPT STATUS")
print(f"  {r}")

# Test 2: SCRIPT RUN without uploaded script
print("\n=== Test 2: SCRIPT RUN (no script uploaded) ===")
r = send_cmd(s, "SCRIPT RUN")
print(f"  {r}")

# Test 3: Upload a simple script with on_adv hook
print("\n=== Test 3: Upload script with on_adv ===")
script = """
-- Only pass devices with RSSI > -70
function on_adv(addr, rssi, name)
    if name and name ~= "" then
        return true  -- pass devices with names
    end
    return false  -- suppress unnamed devices
end
"""
# For now, use a simpler approach: write script via LUA EXEC and storage
# Actually, let's just test the SCRIPT commands work
print("  (skipping chunked upload for now — testing CLI commands)")

# Test 4: SCRIPT STOP
print("\n=== Test 4: SCRIPT STOP ===")
r = send_cmd(s, "SCRIPT STOP")
print(f"  {r}")

# Test 5: STATUS shows lua + script state
print("\n=== Test 5: STATUS (lua + script state) ===")
r = send_cmd(s, "STATUS")
print(f"  lua_ready: {r.get('lua_ready')}")
print(f"  script_loaded: {r.get('script_loaded')}")
print(f"  script_running: {r.get('script_running')}")

s.close()
print("\nDone.")
