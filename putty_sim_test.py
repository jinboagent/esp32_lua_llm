"""PuTTY-style interactive session simulation (bare CR line endings).

Walks the manual-test checklist over COM12 and prints a transcript.
"""
import serial
import time

s = serial.Serial("COM12", 115200, timeout=1)
time.sleep(0.5)


def cmd(c, read_s=1.0):
    s.reset_input_buffer()
    s.write(c.encode() + b"\r")          # PuTTY sends bare CR on Enter
    end = time.time() + read_s
    out = b""
    while time.time() < end:             # bounded read window (streams
        out += s.read(4096)              # never go silent, so no EOF wait)
    return out.decode(errors="replace")


print("=== PuTTY-style session transcript (CR endings) ===")
print(cmd("STATUS"))
print(cmd("VERSION"))

print(cmd("SCAN START"))
time.sleep(3)
s.reset_input_buffer()
time.sleep(2)
stream = s.read(65536).decode(errors="replace")
adv = [ln for ln in stream.splitlines() if '"addr"' in ln]
print(f"[scan stream: {len(adv)} adv lines in 2 s]")
if adv:
    print("sample:", adv[0][:150])

print(cmd("STATUS"))          # should show scanning:true, received>0
print(cmd("SCAN STOP"))

# Ctrl+C interrupt: start a scan, then a single 0x03 byte (no Enter) must
# stop the stream immediately
print(cmd("SCAN START"))
time.sleep(2)
s.reset_input_buffer()
s.write(b"\x03")
time.sleep(1)
out = s.read(65536).decode(errors="replace")
print("[Ctrl+C] interrupt response:",
      "PASS" if '"cmd":"interrupt"' in out else "FAIL")
time.sleep(1)
s.reset_input_buffer()
time.sleep(2)
quiet = s.read(65536).decode(errors="replace")
adv_after = [ln for ln in quiet.splitlines() if '"addr"' in ln]
print(f"[Ctrl+C] stream stopped: "
      f"{'PASS' if not adv_after else 'FAIL'} ({len(adv_after)} adv after)")
print(cmd("STATUS"))
s.close()
print("=== end ===")
