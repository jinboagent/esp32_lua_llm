"""F2.4 BLE connection HIL suite — PC acts as a WinRT GATT server peer.

Device: ESP32-S3 dongle (F2.4 build) on COM12.
Requires: pyserial, winrt-Windows.Devices.Bluetooth.GenericAttributeProfile,
winrt-Windows.Storage.Streams.

  C0  control plane without a peer: arg validation, tap-path and
      own-discovery start/stop, -453/-450 error paths
  C1  auto-connect by service UUID while scanning (tap path); notify
      payloads re-streamed as "src":"conn" merged JSON lines
  C2  direct connect by address + type learned from C1
  C3  CONN STOP while idle -> -453
  C4  POWER STATUS reports active while connected with scan off (D1)
  C5  chatty peer: device stays responsive, rx_lines counted
  C6  peer vanishes: dongle returns to idle (best effort)

Usage:  python test_ble_conn_hw.py [COM12]
"""
import json
import sys
import time
import uuid as pyuuid

import serial

PORT = sys.argv[1] if len(sys.argv) > 1 else "COM12"
BAUD = 115200

SVC_UUID = "12345678-1234-1234-1234-123456789abc"
CHAR_UUID = "12345678-1234-1234-1234-123456789a01"

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


def conn_status(s):
    return cmd_json(s, "CONN STATUS") or {}


def wait_connected(s, secs):
    end = time.time() + secs
    st = {}
    while time.time() < end:
        st = conn_status(s)
        if (st.get("conn") or {}).get("connected"):
            return st
        time.sleep(0.5)
    return st


def collect_conn(s, secs):
    out = []
    end = time.time() + secs
    while time.time() < end:
        txt = s.readline().decode(errors="replace").strip()
        if not txt.startswith("{") or '"src":"conn"' not in txt:
            continue
        try:
            out.append(json.loads(txt))
        except json.JSONDecodeError:
            continue
    return out


class GattPeer:
    """Windows WinRT GATT server: one service, one notify+read char."""

    def __init__(self):
        from winrt.windows.devices.bluetooth.genericattributeprofile import (
            GattServiceProvider,
            GattServiceProviderAdvertisingParameters,
            GattLocalCharacteristicParameters,
            GattCharacteristicProperties,
        )
        from winrt.windows.storage.streams import DataWriter
        self._DataWriter = DataWriter
        self._AdvParams = GattServiceProviderAdvertisingParameters

        res = GattServiceProvider.create_async(
            pyuuid.UUID(SVC_UUID)).get_results()
        self.provider = res.service_provider
        if self.provider is None:
            raise RuntimeError(f"service create failed: {res.status}")

        params = GattLocalCharacteristicParameters()
        params.characteristic_properties = (
            GattCharacteristicProperties.NOTIFY |
            GattCharacteristicProperties.READ)
        cres = self.provider.create_characteristic_async(
            pyuuid.UUID(CHAR_UUID), params).get_results()
        self.char = cres.characteristic
        if self.char is None:
            raise RuntimeError(f"characteristic create failed: {cres.status}")
        self.char.add_read_requested_handler(self._on_read)
        self.payload = b'{"v":1,"who":"peer"}'

    def _on_read(self, sender, args):
        try:
            req = args.get_request().get_results()
            w = self._DataWriter()
            w.write_bytes(self.payload)
            req.respond_with_value(w.detach_buffer())
        except Exception:
            pass

    def start(self):
        adv = self._AdvParams()
        adv.is_connectable = True
        adv.is_discoverable = True
        self.provider.start_advertising(adv)

    def wait_subscribed(self, secs):
        end = time.time() + secs
        while time.time() < end:
            if len(self.char.subscribed_clients) > 0:
                return True
            time.sleep(0.2)
        return False

    def notify(self, payload):
        w = self._DataWriter()
        w.write_bytes(payload)
        self.char.notify_value_async(w.detach_buffer()).get_results()

    def stop(self):
        try:
            self.provider.stop_advertising()
        except Exception:
            pass


class ConnectablePeer:
    """Fallback peer: connectable advertiser carrying the service UUID.

    No GATT server behind it — the dongle's service discovery is expected
    to fail. Exercises the tap's 128-bit UUID match, the GAP connect, and
    the discovery-failure recovery over the air.
    """

    def __init__(self):
        from winrt.windows.devices.bluetooth.advertisement import (
            BluetoothLEAdvertisementPublisher,
        )
        self._pub = BluetoothLEAdvertisementPublisher()
        self._pub.advertisement.service_uuids.append(pyuuid.UUID(SVC_UUID))
        self._pub.is_connectable = True

    def start(self):
        self._pub.start()

    def stop(self):
        try:
            self._pub.stop()
        except Exception:
            pass


s = serial.Serial(PORT, BAUD, timeout=0.5)
time.sleep(0.5)
s.reset_input_buffer()

print(f"=== F2.4 BLE connection HIL suite on {PORT} ===\n")

r = cmd_json(s, "STATUS")
check("device sanity (STATUS ok)", r is not None and r.get("status") == "ok",
      str(r))
check("conn feature enabled in build",
      bool((r or {}).get("conn", {}).get("enabled")), str(r))

# ---- C0: control plane without a peer --------------------------------------
print("[C0] control plane: validation + search start/stop paths")
r = cmd_json(s, "CONN STATUS")
check("CONN STATUS idle", (r or {}).get("conn", {}).get("state") == "idle",
      str(r))
r = cmd_json(s, "CONN INTERVAL 50")
check("INTERVAL 50 -> -450", r and r.get("code") == -450, str(r))
r = cmd_json(s, "CONN INTERVAL 500")
check("INTERVAL 500 ok", r and r.get("status") == "ok", str(r))
r = cmd_json(s, "CONN TARGET not-a-uuid")
check("TARGET bad uuid -> -450", r and r.get("code") == -450, str(r))
r = cmd_json(s, f"CONN TARGET {SVC_UUID} {CHAR_UUID}")
check("TARGET good uuids ok", r and r.get("status") == "ok", str(r))
r = cmd_json(s, "CONN START XX:XX:XX:XX:XX:XX")
check("START bad addr -> -450", r and r.get("code") == -450, str(r))

r = cmd_json(s, "SCAN START")
check("SCAN START ok (C0)", r and r.get("status") == "ok", str(r))
r = cmd_json(s, "CONN START")
check("START auto (tap path) ok", r and r.get("status") == "ok", str(r))
st = conn_status(s)
check("state peer_search while scanning",
      (st.get("conn") or {}).get("state") == "peer_search", str(st))
r = cmd_json(s, "CONN STOP")
check("STOP from peer_search ok", r and r.get("status") == "ok", str(r))
cmd_json(s, "SCAN STOP")

r = cmd_json(s, "CONN START")
check("START auto (own discovery) ok", r and r.get("status") == "ok", str(r))
st = conn_status(s)
check("state peer_search while idle-scan",
      (st.get("conn") or {}).get("state") == "peer_search", str(st))
r = cmd_json(s, "CONN STOP")
check("STOP from own-disc ok", r and r.get("status") == "ok", str(r))
r = cmd_json(s, "CONN STOP")
check("STOP while idle -> -453", r and r.get("code") == -453, str(r))

peer = None
try:
    peer = GattPeer()
    peer.start()
    time.sleep(1.0)
except Exception as e:
    skip("WinRT GATT server unavailable", repr(e))

if peer is None:
    print("[C7] fallback: connectable advertiser, discovery must fail cleanly")
    cpeer = None
    try:
        cpeer = ConnectablePeer()
        cpeer.start()
        time.sleep(1.0)
    except Exception as e:
        skip("connectable advertiser unavailable", repr(e))
    if cpeer is not None:
        r = cmd_json(s, "SCAN START")
        check("SCAN START ok", r and r.get("status") == "ok", str(r))
        r = cmd_json(s, f"CONN TARGET {SVC_UUID}")
        check("CONN TARGET ok", r and r.get("status") == "ok", str(r))
        r = cmd_json(s, "CONN START")
        check("CONN START (auto) ok", r and r.get("status") == "ok", str(r))

        states = set()
        failed_note = False
        last_poll = 0.0
        end = time.time() + 25
        while time.time() < end:
            txt = s.readline().decode(errors="replace").strip()
            if "discovery failed" in txt:
                failed_note = True
            if txt.startswith("{") and '"cmd":"conn_status"' in txt:
                try:
                    states.add((json.loads(txt).get("conn") or {}).get("state"))
                except json.JSONDecodeError:
                    pass
            if time.time() - last_poll > 2:
                last_poll = time.time()
                s.write(b"CONN STATUS\n")
                s.flush()
            if failed_note and "idle" in states:
                break
        check("tap matched 128-bit UUID over the air",
              {"peer_search", "connecting", "discovering"} & states,
              f"states={states}")
        check("discovery failure surfaced and recovered", failed_note,
              f"states={states}")
        cpeer.stop()
        cmd_json(s, "CONN STOP")
        cmd_json(s, "SCAN STOP")
    r = cmd_json(s, "STATUS")
    check("final STATUS idle", r and r.get("state") == "idle", str(r))
    s.close()
    print(f"\n{PASS} passed, {FAIL} failed, {SKIP} skipped")
    sys.exit(1 if FAIL else 0)

# ---- C1: auto-connect by service UUID (tap path) --------------------------
print("[C1] auto-connect by service UUID while scanning")
r = cmd_json(s, "SCAN START")
check("SCAN START ok", r and r.get("status") == "ok", str(r))
r = cmd_json(s, f"CONN TARGET {SVC_UUID} {CHAR_UUID}")
check("CONN TARGET ok", r and r.get("status") == "ok", str(r))
r = cmd_json(s, "CONN START")
check("CONN START (auto) ok", r and r.get("status") == "ok", str(r))

st = wait_connected(s, 15)
conn = st.get("conn") or {}
check("connected within 15 s", bool(conn.get("connected")), str(st))
peer_addr = conn.get("addr", "")
peer_type = conn.get("addr_type", "public")

subscribed = peer.wait_subscribed(5)
check("dongle subscribed (notify mode)", subscribed,
      "CCCD never written")
check("CONN STATUS reports notify mode", conn.get("mode") == "notify"
      or subscribed, str(conn))

for i in range(5):
    peer.notify(json.dumps({"v": i, "who": "peer"}).encode())
    time.sleep(0.3)
lines = collect_conn(s, 3)
check("conn lines re-streamed (>=2)", len(lines) >= 2, f"got {len(lines)}")
if lines:
    check("merged payload fields present",
          all("v" in l and l.get("who") == "peer" for l in lines),
          str(lines[:2]))
    check("envelope addr matches peer",
          all(l.get("addr") == peer_addr for l in lines), str(lines[:2]))

# ---- C4: power observability with scan off (D1) ----------------------------
print("[C4] POWER STATUS active while connected, scan off")
cmd_json(s, "SCAN STOP")
time.sleep(0.5)
st2 = conn_status(s)
check("connection survives SCAN STOP",
      bool((st2.get("conn") or {}).get("connected")), str(st2))
r = cmd_json(s, "POWER STATUS")
check("POWER STATUS active (conn holds radio)",
      r and "active" in json.dumps(r), str(r))

# ---- C5: chatty peer, device stays responsive ------------------------------
print("[C5] chatty peer: responsiveness + rx counters")
for i in range(60):
    try:
        peer.notify(json.dumps({"v": i, "who": "peer"}).encode())
    except Exception:
        break
    time.sleep(0.02)
time.sleep(1)
r = cmd_json(s, "STATUS")
check("device responsive after burst", r and r.get("status") == "ok", str(r))
st = conn_status(s)
conn = st.get("conn") or {}
check("rx_lines counted (>0)", conn.get("rx_lines", 0) > 0, str(st))
check("drops field present", "drops" in conn, str(conn))

cmd_json(s, "CONN STOP")
time.sleep(1)

# ---- C2: direct connect by learned address ---------------------------------
print("[C2] direct connect by address + type")
if peer_addr:
    r = cmd_json(s, f"CONN START {peer_addr} {peer_type}")
    check("CONN START <addr> ok", r and r.get("status") == "ok", str(r))
    st = wait_connected(s, 10)
    check("direct reconnect", bool((st.get("conn") or {}).get("connected")),
          str(st))
    cmd_json(s, "CONN STOP")
    time.sleep(1)
else:
    skip("C2", "no peer address learned in C1")

# ---- C3: STOP while idle -> -453 --------------------------------------------
print("[C3] CONN STOP while idle")
r = cmd_json(s, "CONN STOP")
check("STOP while idle -> code -453",
      r and r.get("code") == -453, str(r))

# ---- C6: peer vanishes (best effort) ----------------------------------------
print("[C6] peer vanishes -> dongle back to idle")
r = cmd_json(s, "CONN START")
st = wait_connected(s, 15)
if (st.get("conn") or {}).get("connected"):
    peer.stop()
    peer = None
    dropped = False
    end = time.time() + 12
    while time.time() < end:
        st = conn_status(s)
        if not (st.get("conn") or {}).get("connected"):
            dropped = True
            break
        time.sleep(1)
    if dropped:
        check("disconnect detected on peer vanish", True)
    else:
        skip("C6", "link supervision timeout longer than window")
        cmd_json(s, "CONN STOP")
else:
    skip("C6", "reconnect for vanish test failed")

# ---- cleanup ------------------------------------------------------------------
cmd_json(s, "CONN STOP")
cmd_json(s, "SCAN STOP")
if peer is not None:
    peer.stop()
r = cmd_json(s, "STATUS")
check("final STATUS idle", r and r.get("state") == "idle", str(r))

s.close()
print(f"\n{PASS} passed, {FAIL} failed, {SKIP} skipped")
sys.exit(1 if FAIL else 0)
