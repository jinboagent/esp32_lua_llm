"""Verify the console accepts both CR (PuTTY Enter) and LF (scripts)."""
import serial
import time

s = serial.Serial("COM12", 115200, timeout=2)
time.sleep(0.5)

s.reset_input_buffer()
s.write(b"STATUS\r")          # PuTTY-style: bare CR
time.sleep(1)
d = s.read(4096).decode(errors="replace")
print("CR-test :", "PASS" if '"status":"ok"' in d else "FAIL")
if '"status":"ok"' not in d:
    print("   got:", d[:200])

s.reset_input_buffer()
s.write(b"STATUS\n")          # script-style: LF (regression)
time.sleep(1)
d = s.read(4096).decode(errors="replace")
print("LF-test :", "PASS" if '"status":"ok"' in d else "FAIL")

s.reset_input_buffer()
s.write(b"STATUS\r\n")        # CRLF pair: must yield exactly one response
time.sleep(1)
d = s.read(4096).decode(errors="replace")
n = d.count('"status":"ok"')
print("CRLF-test:", "PASS" if n == 1 else f"FAIL ({n} responses)")

s.close()
