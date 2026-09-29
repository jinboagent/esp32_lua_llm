"""Capture 25 s of raw console stream after SCAN START.

Looks for: (a) adv lines with ts > 11000 (proof the DISC_COMPLETE restart
works past the 10.24 s discovery window), (b) panic/assert/boot-banner text
(proof of a crash + reboot).
"""
import serial
import time

s = serial.Serial("COM12", 115200, timeout=1)
time.sleep(0.5)
s.reset_input_buffer()
s.write(b"SCAN START\n")

end = time.time() + 25
buf = b""
while time.time() < end:
    buf += s.read(4096)

s.write(b"SCAN STOP\n")
time.sleep(1)
buf += s.read(4096)
s.close()

text = buf.decode(errors="replace")
lines = [ln for ln in text.splitlines() if ln.strip()]

adv = [ln for ln in lines if '"addr"' in ln]
max_ts = 0
for ln in adv:
    try:
        ts = int(ln.split('"ts":')[1].split(",")[0])
        max_ts = max(max_ts, ts)
    except (IndexError, ValueError):
        pass

print(f"total lines : {len(lines)}")
print(f"adv lines   : {len(adv)}")
print(f"max ts (ms) : {max_ts}  {'<-- PAST 10.24s WINDOW: restart WORKS' if max_ts > 11000 else '<-- scan died before/at the window'}")

bad = [ln for ln in lines if any(k in ln for k in
       ("Guru Meditation", "abort", "assert", "panic", "Backtrace",
        "=== BLE Bridge Dongle", "rst:", "cpu_start"))]
if bad:
    print("--- CRASH/REBOOT EVIDENCE ---")
    for ln in bad[:12]:
        print(ln[:160])
else:
    print("no crash/reboot markers in stream")

# N2 detail: every non-adv console line (BLE/pipeline/restart messages)
other = [ln for ln in lines if '"addr"' not in ln and '"status"' not in ln
         and not ln.startswith(">")]
print("--- non-adv console lines ---")
for ln in other[:20]:
    print(ln[:160])
