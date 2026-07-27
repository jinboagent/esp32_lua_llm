import serial, time

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
    for l in out:
        print(l)
    return out

print("--- BEGIN ---")
send("SCRIPT BEGIN")

print("--- CHUNK ---")
script = "function on_adv(a,r,n) return true end"
send("SCRIPT CHUNK " + script.encode().hex())

print("--- END ---")
send("SCRIPT END", wait=3)

print("--- STATUS ---")
send("SCRIPT STATUS")

print("--- RUN ---")
send("SCRIPT RUN", wait=3)

print("--- STATUS after run ---")
send("SCRIPT STATUS")

s.close()
print("Done.")
