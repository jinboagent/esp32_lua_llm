"""Controlled BLE peer + connection-boundary hardware suite.

Device: ESP32-S3 dongle on COM12.  Requires: pyserial, bleak,
winrt-Windows.Devices.Bluetooth.Advertisement, winrt-Windows.Storage.Streams.

The PC's Bluetooth radio acts as a deterministic BLE counterpart so the
data plane can be verified without depending on ambient RF:

  P1  PC advertises mfg id=FFFF data="SNF"+AABBCC; the dongle must capture
      it with exactly those fields (name-based advertising is rejected by
      this Windows adapter, so the peer is identified by manufacturer data).
  P2  dedup cadence on the controlled peer (~1 line/s while it advertises).
  P3  connection boundary: while the dongle scans, the PC scans back —
      the dongle must NOT be discoverable/connectable (no advertisement /
      GATT server; F2.4 added an optional central role, but the dongle
      still never accepts connections). This test pins that boundary.
  P4  radio contention: dongle keeps streaming while the PC adapter runs
      its own active BLE scan.

Usage:  python test_ble_peer_hw.py [COM12]
"""
import asyncio
import json
import sys
import time

import serial

PORT = sys.argv[1] if len(sys.argv) > 1 else "COM12"
BAUD = 115200
# Windows 10 (build 19045, MediaTek adapter) rejects local_name in the
# advertisement payload with E_INVALIDARG, but accepts manufacturer data.
# The peer is therefore identified by mfg id + payload marker, not name.
MFG_ID = 0xFFFF
MFG_MARKER = b"SNF"                 # ASCII marker so we can spot our peer
MFG_DATA = MFG_MARKER + bytes([0xAA, 0xBB, 0xCC])
MFG_DATA_HEX = MFG_DATA.hex().upper()

PASS = 0
FAIL = 0
SKIP = 0


def check(name, cond, detail=""):
    global PASS, FAIL
    if cond:
        PASS += 1
        print(f"  [PASS] {name}")
    else:
        FAIL += 1
        print(f"  [FAIL] {name}  {detail}")


def skip(name, reason):
    global SKIP
    SKIP += 1
    print(f"  [SKIP] {name}  {reason}")


def cmd_json(s, line, timeout=4.0):
    s.reset_input_buffer()
    s.write((line + "\n").encode())
    s.flush()
    end = time.time() + timeout
    while time.time() < end:
        txt = s.readline().decode(errors="replace").strip()
        if txt.startswith("{") and '"status"' in txt:
            try:
                return json.loads(txt)
            except json.JSONDecodeError:
                pass
    return None


def collect(s, secs, peer_only=False):
    """Collect adv JSON lines for secs seconds (optionally only our peer)."""
    out = []
    end = time.time() + secs
    while time.time() < end:
        txt = s.readline().decode(errors="replace").strip()
        if not txt.startswith("{") or '"addr"' not in txt:
            continue
        try:
            j = json.loads(txt)
        except json.JSONDecodeError:
            continue
        if peer_only:
            m = j.get("manu") or {}
            if m.get("id") != f"{MFG_ID:04X}":
                continue
            if not str(m.get("data", "")).startswith(MFG_DATA_HEX):
                continue
        out.append(j)
    return out


class PeerAdvertiser:
    """Windows WinRT BLE advertiser (peripheral role for the PC).

    Win10 quirk: setting local_name makes Start() fail with E_INVALIDARG
    on this adapter, so the peer is recognized by its manufacturer data.
    """

    def __init__(self):
        from winrt.windows.devices.bluetooth.advertisement import (
            BluetoothLEAdvertisementPublisher,
            BluetoothLEManufacturerData,
        )
        from winrt.windows.storage.streams import DataWriter

        writer = DataWriter()
        writer.write_bytes(MFG_DATA)

        self._pub = BluetoothLEAdvertisementPublisher()
        mfg = BluetoothLEManufacturerData(MFG_ID, writer.detach_buffer())
        self._pub.advertisement.manufacturer_data.append(mfg)

    def start(self):
        self._pub.start()

    def stop(self):
        try:
            self._pub.stop()
        except Exception:
            pass


async def pc_discover(timeout_s):
    from bleak import BleakScanner
    return await BleakScanner.discover(timeout=timeout_s)


s = serial.Serial(PORT, BAUD, timeout=0.5)
time.sleep(0.5)
s.reset_input_buffer()

print(f"=== Controlled BLE peer suite on {PORT} ===\n")

r = cmd_json(s, "STATUS")
check("device sanity (STATUS)", r is not None and r.get("status") == "ok", str(r))

# ---- P1 + P2: controlled peer capture -------------------------------------
print("[P1] controlled peer: PC advertises, dongle must capture it")
peer = None
try:
    peer = PeerAdvertiser()
    peer.start()
except Exception as e:
    skip("WinRT advertiser unavailable", repr(e))

if peer is not None:
    r = cmd_json(s, "SCAN START")
    check("SCAN START ok", r and r.get("status") == "ok", str(r))
    time.sleep(1)
    peer_lines = collect(s, 8, peer_only=True)
    # WinRT does not let us set the advertising interval; with the 1 s
    # dedup window, >=2 exact-field captures in 8 s proves the path.
    check("peer captured (>=2 lines in 8 s)", len(peer_lines) >= 2,
          f"got {len(peer_lines)}")
    if peer_lines:
        mfg = peer_lines[0].get("manu") or {}
        check("peer manu id FFFF", mfg.get("id") == f"{MFG_ID:04X}", str(mfg))
        check("peer manu data exact match",
              str(mfg.get("data", "")) == MFG_DATA_HEX, str(mfg))
        check("peer rssi present", all("rssi" in p for p in peer_lines))
        check("peer addr stable",
              len({p.get("addr") for p in peer_lines}) == 1,
              str({p.get("addr") for p in peer_lines}))

        # P2 dedup cadence: ~1 line/s for a continuous advertiser
        more = collect(s, 6, peer_only=True)
        check("dedup cadence 2..8 lines per 6 s", 2 <= len(more) <= 8,
              f"got {len(more)}")
    peer.stop()
    cmd_json(s, "SCAN STOP")
else:
    r = cmd_json(s, "SCAN START")   # keep coverage moving without a peer
    time.sleep(2)
    cmd_json(s, "SCAN STOP")

# ---- P3: connection boundary (v1: dongle must not be discoverable) --------
print("[P3] connection boundary: PC scans for the dongle while it scans")
r = cmd_json(s, "SCAN START")
try:
    devices = asyncio.run(pc_discover(8.0))
    names = [d.name for d in devices if d.name]
    bridge_seen = any("BLE-Bridge" in (n or "") for n in names) or \
        any("Bridge" in (n or "") for n in names)
    check("dongle NOT discoverable while scanning (v1 passive-only)",
          not bridge_seen, f"names seen: {names}")
    print(f"        (PC saw {len(devices)} devices: "
          f"{', '.join(sorted(set(names))[:6]) or 'none named'})")
except Exception as e:
    skip("bleak scan failed", repr(e))

# ---- P4: radio contention ---------------------------------------------------
print("[P4] radio contention: PC scanning while dongle streams")
try:
    import threading

    def run_pc_scan():
        asyncio.run(pc_discover(6.0))

    t = threading.Thread(target=run_pc_scan, daemon=True)
    t.start()
    busy_lines = collect(s, 6)
    t.join(timeout=10)
    check("dongle still streams while PC radio busy", len(busy_lines) > 0,
          "no lines during contention")
except Exception as e:
    skip("contention test failed to run", repr(e))

# ---- cleanup ------------------------------------------------------------------
cmd_json(s, "SCRIPT STOP")
cmd_json(s, "SCAN STOP")
r = cmd_json(s, "STATUS")
check("final STATUS idle", r and r.get("state") == "idle", str(r))

s.close()
print(f"\n{PASS} passed, {FAIL} failed, {SKIP} skipped")
sys.exit(1 if FAIL else 0)
