"""Conn-plane soak — long-session stability the suites can't cover.

C6's supervision window is a permanent SKIP and every suite observes
only seconds of a connection; this tool holds a conn session for
minutes/hours against the PC peer (embedded GattPeer copy, self-
contained per house style), notifies a counter payload at 1 Hz, and
samples:

  CONN STATUS counters (connects/disconnects/rx_notify/rx_read/
  tx_lines/dropped/errors) and STATUS health (free_heap, lua_pool
  used/peak) every --sample-every seconds, into a JSONL file plus a
  live one-line status. Optional --reconnect-every N exercises the
  full disconnect -> settle -> reconnect cycle N seconds apart
  (STOP ok is NOT state off: the settle polls CONN STATUS to "off"
  before every START, the 2026-08-28 race).

Exit 0 when the run completes with rx lines still flowing and no
error/drop growth; exit 1 otherwise. The port stays open for the whole
run (N3).

Usage:
  python tests/hw/soak_conn.py [port] [--secs 3600] [--sample-every 30]
      [--reconnect-every 0] [--out conn_soak.jsonl]
"""
import argparse
import asyncio
import json
import sys
import threading
import time
import uuid as pyuuid

import serial

BAUD = 115200
SVC_UUID = "12345678-1234-1234-1234-123456789abc"
CHR_UUID = "12345678-1234-1234-1234-123456789a01"
SETTLE_TIMEOUT = 8.0
ACTIVE_TIMEOUT = 15.0


class GattPeer:
    """Adapted copy of the hw-suite peer (asyncio/await WinRT server)."""

    def __init__(self):
        self.provider = None
        self.char = None
        self._adv = {"status": None}
        self._loop = asyncio.new_event_loop()
        threading.Thread(target=self._loop.run_forever,
                         daemon=True).start()

    async def _setup(self):
        from winrt.windows.devices.bluetooth import BluetoothError
        from winrt.windows.devices.bluetooth.genericattributeprofile import (
            GattServiceProvider,
            GattLocalCharacteristicParameters,
            GattCharacteristicProperties,
        )
        from winrt.windows.storage.streams import DataWriter
        res = await GattServiceProvider.create_async(
            pyuuid.UUID(SVC_UUID))
        self.provider = res.service_provider
        if self.provider is None or int(res.error) != int(
                BluetoothError.SUCCESS):
            raise RuntimeError(f"service create failed: {res.error}")
        params = GattLocalCharacteristicParameters()
        params.characteristic_properties = (
            GattCharacteristicProperties.NOTIFY
            | GattCharacteristicProperties.READ)
        cres = await self.provider.service.create_characteristic_async(
            pyuuid.UUID(CHR_UUID), params)
        self.char = cres.characteristic
        if self.char is None:
            raise RuntimeError("characteristic create failed")

        async def handle_read(args):
            req = await args.get_request_async()
            w = DataWriter()
            w.write_bytes(json.dumps({"v": 0}).encode())
            req.respond_with_value(w.detach_buffer())

        def on_read(sender, args):
            d = args.get_deferral()

            def done(f):
                try:
                    f.result()
                except Exception:
                    pass
                finally:
                    d.complete()
            asyncio.run_coroutine_threadsafe(
                handle_read(args), self._loop).add_done_callback(done)

        self._on_read = on_read
        self.char.add_read_requested(on_read)

    async def _advertise(self):
        from winrt.windows.devices.bluetooth.genericattributeprofile import (
            GattServiceProviderAdvertisingParameters,
            GattServiceProviderAdvertisementStatus,
        )
        st = GattServiceProviderAdvertisementStatus

        def on_status(sender, args):
            self._adv["status"] = int(args.status)
            print(f"  [peer] advertisement status -> {int(args.status)}",
                  flush=True)
        self._on_status = on_status
        self.provider.add_advertisement_status_changed(on_status)
        adv = GattServiceProviderAdvertisingParameters()
        adv.is_connectable = True
        adv.is_discoverable = True
        self.provider.start_advertising_with_parameters(adv)
        end = time.monotonic() + 10
        started = {int(st.STARTED),
                   int(st.STARTED_WITHOUT_ALL_ADVERTISEMENT_DATA)}
        while time.monotonic() < end:
            if self._adv["status"] in started:
                return
            await asyncio.sleep(0.05)
        raise RuntimeError("advertisement never reached STARTED")

    async def _notify(self, payload):
        from winrt.windows.storage.streams import DataWriter
        w = DataWriter()
        w.write_bytes(payload)
        await self.char.notify_value_async(w.detach_buffer())

    async def _subscribed(self):
        try:
            return len(list(self.char.subscribed_clients))
        except Exception:
            return 0

    def start(self):
        asyncio.run_coroutine_threadsafe(
            self._setup(), self._loop).result(15)
        asyncio.run_coroutine_threadsafe(
            self._advertise(), self._loop).result(15)

    def wait_subscribed(self, secs):
        end = time.time() + secs
        while time.time() < end:
            n = asyncio.run_coroutine_threadsafe(
                self._subscribed(), self._loop).result(2)
            if n > 0:
                return True
            time.sleep(0.2)
        return False

    def notify(self, payload):
        asyncio.run_coroutine_threadsafe(
            self._notify(payload), self._loop).result(5)

    def stop(self):
        if self.provider is not None:
            try:
                self._loop.call_soon_threadsafe(
                    self.provider.stop_advertising)
            except Exception:
                pass
        self._loop.call_soon_threadsafe(self._loop.stop)


def expected_cmd(line):
    words = line.split()
    if len(words) >= 2 and words[0] in ("SCAN", "CONN", "SCRIPT", "POWER"):
        return (words[0] + "_" + words[1]).lower()
    if words and words[0] in ("STATUS", "VERSION"):
        return words[0].lower()
    return None


def cmd_json(s, line, timeout=4.0):
    want = expected_cmd(line)
    s.reset_input_buffer()
    s.write((line + "\n").encode())
    s.flush()
    end = time.time() + timeout
    while time.time() < end:
        raw = s.readline()
        if not raw:
            continue
        txt = raw.decode(errors="replace").strip()
        if txt.startswith("{") and '"status"' in txt:
            try:
                obj = json.loads(txt)
            except json.JSONDecodeError:
                continue
            if isinstance(obj, dict) and "status" in obj and (
                    want is None or obj.get("cmd") == want):
                return obj
    return None


def drain(s, secs):
    s.timeout = 0.05
    end = time.time() + secs
    n = 0
    while time.time() < end:
        if s.readline():
            n += 1
    return n


def wait_state(s, want, timeout):
    end = time.time() + timeout
    while time.time() < end:
        r = cmd_json(s, "CONN STATUS")
        if r and r.get("state") == want:
            return r
        time.sleep(0.5)
    return None


def connect(s, settle=True):
    if settle:
        cmd_json(s, "CONN STOP", 8.0)
        wait_state(s, "off", SETTLE_TIMEOUT)
    r = cmd_json(s, f"CONN TARGET {SVC_UUID} {CHR_UUID}")
    if not (r and r.get("status") == "ok"):
        return False
    r = cmd_json(s, "CONN START", 15.0)
    if not (r and r.get("status") == "ok"):
        return False
    return wait_state(s, "active", ACTIVE_TIMEOUT) is not None


def main():
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("port", nargs="?", default="COM12")
    p.add_argument("--secs", type=float, default=3600.0)
    p.add_argument("--sample-every", type=float, default=30.0)
    p.add_argument("--reconnect-every", type=float, default=0.0,
                   help="0 = hold one session; N = disconnect/settle/"
                        "reconnect every N seconds")
    p.add_argument("--out", default="conn_soak.jsonl")
    args = p.parse_args()

    s = serial.Serial(args.port, BAUD, timeout=1)
    time.sleep(0.8)
    s.reset_input_buffer()
    peer = GattPeer()
    peer.start()
    time.sleep(1.0)
    print(f"=== conn soak: {args.secs:.0f}s, sample every "
          f"{args.sample_every:.0f}s, reconnect every "
          f"{args.reconnect_every:.0f}s (0=off) ===")
    if not connect(s):
        print("error: initial connect failed — aborting soak")
        peer.stop()
        s.close()
        return 1
    print("connected; waiting for subscription")
    peer.wait_subscribed(8)

    rx_first = None
    prev = {}
    samples = 0
    rcv_lines = 0
    failures = []
    start = time.time()
    next_reconnect = (start + args.reconnect_every
                      if args.reconnect_every > 0 else None)
    with open(args.out, "w", encoding="utf-8") as f:
        while True:
            t = time.time() - start
            if t >= args.secs:
                break
            # notify at 1 Hz
            payload = json.dumps({"t": round(t, 1), "v": int(t)},
                                 separators=(",", ":")).encode()
            try:
                peer.notify(payload)
            except Exception as e:
                failures.append(f"notify@{t:.0f}s: {e!r}")
            # drain + count conn lines
            got = 0
            end = time.time() + 0.9
            s.timeout = 0.05
            while time.time() < end:
                raw = s.readline()
                if raw:
                    txt = raw.decode(errors="replace").strip()
                    if txt.startswith("{") and '"src":"conn"' in txt:
                        got += 1
            rcv_lines += got
            if rx_first is None and got:
                rx_first = t
            # periodic sample
            if samples == 0 or t >= samples * args.sample_every:
                cs = cmd_json(s, "CONN STATUS") or {}
                st = cmd_json(s, "STATUS") or {}
                rec = {"t": round(t, 1),
                       "conn": {k: cs.get(k) for k in
                                ("state", "connects", "disconnects",
                                 "rx_notify", "rx_read", "tx_lines",
                                 "dropped", "errors")},
                       "heap": st.get("free_heap"),
                       "pool": st.get("lua_pool")}
                f.write(json.dumps(rec) + "\n")
                f.flush()
                samples += 1
                print(f"t={t:6.0f}s conn={rec['conn'].get('state')} "
                      f"rx_notify={rec['conn'].get('rx_notify')} "
                      f"dropped={rec['conn'].get('dropped')} "
                      f"errors={rec['conn'].get('errors')} "
                      f"heap={rec['heap']}")
                prev = rec
            # reconnect cycle
            if next_reconnect and time.time() >= next_reconnect:
                next_reconnect += args.reconnect_every
                cmd_json(s, "CONN STOP", 8.0)
                wait_state(s, "off", SETTLE_TIMEOUT)
                if not connect(s, settle=False):
                    failures.append(f"reconnect@{t:.0f}s failed")
                else:
                    peer.wait_subscribed(8)

    cs = cmd_json(s, "CONN STATUS") or {}
    cmd_json(s, "CONN STOP", 8.0)
    wait_state(s, "off", SETTLE_TIMEOUT)
    cmd_json(s, "SCAN STOP")
    peer.stop()
    s.close()

    print(f"--- summary: {samples} samples, {rcv_lines} conn lines, "
          f"first line after {rx_first if rx_first is not None else -1:.0f}s")
    print(f"final: {json.dumps({k: cs.get(k) for k in ('connects', 'disconnects', 'rx_notify', 'dropped', 'errors')})}")
    healthy = (rcv_lines > 0
               and not failures
               and (cs.get("errors") or 0) == 0
               and (cs.get("dropped") or 0) == 0)
    if failures:
        for x in failures[:10]:
            print("failure:", x)
    print("result:", "PASS" if healthy else "FAIL")
    return 0 if healthy else 1


if __name__ == "__main__":
    sys.exit(main())
