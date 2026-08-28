import serial, time

port = "COM12"
baud = 115200

tests = [
    ("math.pi",            'LUA EXEC return math.pi'),
    ("os.execute blocked", 'LUA EXEC os.execute("ls")'),
    ("io blocked",         'LUA EXEC return io'),
    ("timeout",            'LUA EXEC while true do end'),
    ("table works",        'LUA EXEC return table.concat({"a","b","c"}, "-")'),
    ("syntax error",       'LUA EXEC return +++'),
]

s = serial.Serial(port, baud, timeout=5)
time.sleep(0.5)
s.reset_input_buffer()

for name, cmd in tests:
    s.write((cmd + "\n").encode())
    s.flush()
    time.sleep(2)
    lines = []
    while s.in_waiting > 0:
        line = s.readline().decode(errors='replace').strip()
        if line:
            lines.append(line)
    # Find the JSON response
    json_line = [l for l in lines if l.startswith('{')]
    result = json_line[0] if json_line else "(no response)"
    print(f"[{name:20s}] {result}")
    s.reset_input_buffer()

s.close()
print("\nDone.")
