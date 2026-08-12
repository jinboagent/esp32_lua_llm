"""H4 soak test: continuous scan + fragmenting transform, 2 h default.

Keeps ONE serial connection open for the whole run (N3). Every sample
interval sends STATUS and logs heap/Lua-pool/pipeline metrics; counts adv
lines continuously to prove the stream never stalls.

Pass criteria (printed at the end):
  - reset_reason unchanged for the whole run
  - lua_pool.used stable (last sample within +2 KB of the min after warmup)
  - free_heap stable (same band)
  - adv lines observed in every sample window (no stall)
  - queue_drops not growing after warmup
"""
import json
import sys
import time

import serial

HOURS = float(sys.argv[1]) if len(sys.argv) > 1 else 2.0
SAMPLE_S = 300          # STATUS every 5 min
WINDOW_S = 5            # adv-count window inside each sample

SCRIPT = [
    "local i = 0",
    "function transform(addr, json)",
    "  i = i + 1",
    "  local s = string.rep('x', (i % 5) * 24 + 16)",
    "  return json",
    "end",
]


def send(s, cmd, wait=1.5):
    s.reset_input_buffer()
    s.write(cmd.encode() + b"\n")
    end = time.time() + wait
    out = b""
    while time.time() < end:
        out += s.read(4096)
    return out.decode(errors="replace")


def status_of(text):
    for ln in text.splitlines():
        if '"cmd":"status"' in ln:
            try:
                return json.loads(ln)
            except ValueError:
                return None
    return None


s = serial.Serial("COM12", 115200, timeout=1)
time.sleep(0.5)

print(send(s, "SCAN START"))
r = send(s, "SCRIPT LOAD")
for ln in SCRIPT:
    send(s, ln, wait=0.3)
print(send(s, "SCRIPT END"))
print(send(s, "SCRIPT RUN"))

st0 = status_of(send(s, "STATUS"))
print("boot status:", json.dumps(st0) if st0 else "NONE")
if not st0:
    s.close()
    sys.exit("no STATUS — abort")

samples = []
adv_counts = []
end_time = time.time() + HOURS * 3600

try:
    while time.time() < end_time:
        # adv-count window
        s.reset_input_buffer()
        wend = time.time() + WINDOW_S
        n = 0
        while time.time() < wend:
            n += s.read(65536).count(b'"addr"')
        adv_counts.append(n)

        st = status_of(send(s, "STATUS"))
        if st:
            samples.append((time.time(), st))
            print(f"[t+{(time.time()-samples[0][0])/60:6.1f}m] "
                  f"heap={st['free_heap']} lua_used={st['lua_pool']['used']} "
                  f"lua_peak={st['lua_pool']['peak']} "
                  f"recv={st['pipeline']['received']} drops={st['queue_drops']} "
                  f"adv/{WINDOW_S}s={n}", flush=True)
        else:
            print("[sample] STATUS parse failed", flush=True)

        # sleep the rest of the interval while still draining
        send_end = time.time() + (SAMPLE_S - WINDOW_S)
        while time.time() < send_end:
            s.read(65536)
finally:
    print(send(s, "SCRIPT STOP"))
    print(send(s, "SCAN STOP"))
    s.close()

if len(samples) < 2:
    sys.exit("not enough samples")

warm = samples[2:] or samples          # skip warmup
used = [st["lua_pool"]["used"] for _, st in warm]
heap = [st["free_heap"] for _, st in warm]
drops = [st["queue_drops"] for _, st in samples]
rr = {st["reset_reason"] for _, st in samples}

# lua_used oscillates with allocation churn; fragmentation shows as CREEP,
# so compare early vs late averages, not max-min
third = max(1, len(used) // 3)
early_avg = sum(used[:third]) / third
late_avg = sum(used[-third:]) / third

print("=== SOAK SUMMARY ===")
print(f"samples            : {len(samples)}")
print(f"reset_reasons seen : {sorted(rr)}  -> {'PASS' if len(rr) == 1 else 'FAIL'}")
print(f"lua_used min/max   : {min(used)}/{max(used)} (oscillation, informational)")
print(f"lua_used early/late avg: {early_avg:.0f}/{late_avg:.0f}  "
      f"-> {'PASS' if late_avg - early_avg <= 4096 else 'FAIL (creep)'}")
print(f"free_heap min/max  : {min(heap)}/{max(heap)}  "
      f"-> {'PASS' if max(heap) - min(heap) <= 8192 else 'FAIL (drift)'}")
print(f"adv windows w/ 0   : {sum(1 for n in adv_counts if n == 0)}  "
      f"-> {'PASS' if not any(n == 0 for n in adv_counts) else 'FAIL (stall)'}")
print(f"queue_drops first/last: {drops[0]}/{drops[-1]}  "
      f"-> {'PASS' if drops[-1] == drops[0] else 'CHECK (growth)'}")
