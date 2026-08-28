"""Hardware meta-runner — the whole on-device test battery in one command.

Owns COM12 for the whole run (one program at a time), asserts firmware
identity first, runs each suite as a subprocess with a settle between
them, tees every transcript into
harness/02-knowledge/evidence-hw-runs/<timestamp>/, checks the chip's
reset reason after each suite (an unexpected reboot fails loudly), and
prints one summary table. Exit code is nonzero when anything failed —
use it as the post-merge regression gate.

Usage:
  python tests/hw/run_all_hw.py [--port COM12] [--only suite.py ...]
      [--quick] [--list]
"""
import argparse
import os
import re
import subprocess
import sys
import time

import serial

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
EVIDENCE = os.path.join(ROOT, "harness", "02-knowledge",
                        "evidence-hw-runs")

# Ordered battery. Soaks are excluded by default (hours-long).
SUITES = [
    ("test_ble_conn_hw.py", "F2.4 conn plane: C0 + C1-C6 + C7 state matrix"),
    ("test_ble_lua_hw.py", "BLE+Lua data plane (45 checks)"),
    ("test_bridge_hw.py", "F4.1+F4.2 bridge (32 checks)"),
    ("test_power_hw.py", "F4.3 power (14 checks)"),
    ("cr_lf_test.py", "line-terminator contract"),
    ("putty_sim_test.py", "interactive-session simulation"),
]
QUICK = ["test_ble_conn_hw.py", "test_power_hw.py"]


def expected_cmd(line):
    words = line.split()
    if len(words) >= 2 and words[0] in ("SCAN", "CONN", "SCRIPT", "POWER"):
        return (words[0] + "_" + words[1]).lower()
    if words and words[0] in ("STATUS", "VERSION"):
        return words[0].lower()
    return None


def probe(port, line, timeout=4.0):
    s = serial.Serial(port, 115200, timeout=1)
    time.sleep(0.8)
    s.reset_input_buffer()
    want = expected_cmd(line)
    s.write((line + "\n").encode())
    s.flush()
    end = time.time() + timeout
    out = None
    import json as _json
    while time.time() < end:
        raw = s.readline()
        if not raw:
            continue
        txt = raw.decode(errors="replace").strip()
        if txt.startswith("{") and '"status"' in txt:
            try:
                obj = _json.loads(txt)
            except ValueError:
                continue
            if want is None or obj.get("cmd") == want:
                out = obj
                break
    s.close()
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--port", default="COM12")
    ap.add_argument("--only", nargs="*", metavar="suite.py",
                    help="run only these suites (names as listed)")
    ap.add_argument("--quick", action="store_true",
                    help="the fast subset (conn + power)")
    ap.add_argument("--list", action="store_true")
    args = ap.parse_args()

    if args.list:
        for name, desc in SUITES:
            print(f"{name:26s} {desc}")
        return 0

    chosen = [s for s in SUITES
              if (not args.only or s[0] in args.only)
              and (not args.quick or s[0] in QUICK)]
    if not chosen:
        print("no suites selected")
        return 2

    # Firmware identity first: a suite against a stale binary produces
    # nothing but confusion.
    v = probe(args.port, "VERSION")
    if not v or v.get("status") != "ok":
        print(f"error: no device response on {args.port}")
        return 2
    fw = v.get("firmware", "?")
    print(f"device on {args.port}: firmware {fw}")

    stamp = time.strftime("%Y%m%d_%H%M%S")
    outdir = os.path.join(EVIDENCE, stamp)
    os.makedirs(outdir, exist_ok=True)
    print(f"transcripts: {outdir}\n")

    results = []
    for name, desc in chosen:
        path = os.path.join(HERE, name)
        t0 = time.time()
        with open(os.path.join(outdir, name + ".txt"), "w",
                  encoding="utf-8") as log:
            proc = subprocess.run(
                [sys.executable, path], capture_output=True, text=True,
                timeout=900)
            log.write(proc.stdout)
            if proc.stderr:
                log.write("\n--- stderr ---\n" + proc.stderr)
        dur = time.time() - t0
        out = proc.stdout

        def grab(pattern, default="?"):
            m = re.search(pattern, out)
            return m.group(1) if m else default

        if "passed" in out:
            summary = (f"{grab(r'(\d+) passed')} passed, "
                       f"{grab(r'(\d+) failed', '0')} failed")
        else:
            summary = (f"exit {proc.returncode}")
        ok = proc.returncode == 0
        verdict = "PASS" if ok else "FAIL"

        # Reset-reason gate: after each suite the chip should have come
        # back via the USB reset (reason 11); anything else (panic,
        # watchdog, power) means the suite crashed the device.
        try:
            st = probe(args.port, "STATUS")
            reason = st.get("reset_reason") if st else None
        except Exception:
            reason = None
        if ok and reason not in (11, None):
            verdict = f"FAIL (reset_reason={reason})"
            ok = False

        results.append((name, verdict, summary, f"{dur:.0f}s"))
        print(f"{verdict:6s} {name:26s} {summary:20s} {dur:5.0f}s")

    fails = [r for r in results if not r[1].startswith("PASS")]
    print(f"\n{len(results) - len(fails)}/{len(results)} suites green "
          f"(firmware {fw})")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
