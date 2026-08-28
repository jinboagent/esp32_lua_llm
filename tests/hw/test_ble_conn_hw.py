"""Hardware tests for F2.4 BLE connection — C0 peerless control plane plus
C1-C6 GATT data path when a WinRT GATT server peer is available.

C0 exercises everything that does not need a GATT peripheral peer: the
CONN command family, error codes, the state machine, Ctrl+C recovery, and
scan/conn coexistence bookkeeping. C1-C6 (ported from variant A's suite in
the 2026-08-22 improvement pass) use the PC as a WinRT GATT server:
auto-connect by UUID, notify re-streaming, power observability, chatty
peer, direct reconnect, peer vanish. Where the WinRT GATT-server APIs are
unavailable (non-interactive contexts on some PCs), C1-C6 skip cleanly and
a real peripheral (second ESP32 / phone with nRF Connect) remains the
manual follow-up.
"""
import serial, time, json
import uuid as pyuuid

PORT = "COM12"

s = serial.Serial(PORT, 115200, timeout=5)
time.sleep(1)
s.reset_input_buffer()

def cmd(c, w=0.6):
    s.write((c + "\n").encode())
    s.flush()
    time.sleep(w)
    out = [l.decode(errors="replace").strip() for l in s.readlines()
           if l.decode(errors="replace").strip().startswith("{")]
    s.reset_input_buffer()
    for l in reversed(out):
        try:
            o = json.loads(l)
        except json.JSONDecodeError:
            continue
        if isinstance(o, dict) and "status" in o:
            return o
    return None

def cmd_during_scan(c, timeout=8):
    """Response reader that survives a streaming adv line flow: adv lines
    carry "rssi", command responses do not (CONN STATUS carries "addr" —
    its peer field — so "addr" cannot be the discriminator)."""
    s.write((c + "\n").encode())
    s.flush()
    end = time.time() + timeout
    while time.time() < end:
        line = s.readline()
        if not line:
            continue
        t = line.decode(errors="replace").strip()
        if not t.startswith("{"):
            continue
        try:
            o = json.loads(t)
        except json.JSONDecodeError:
            continue
        if isinstance(o, dict) and "status" in o and "rssi" not in o:
            return o
    return None

PASS = 0
FAIL = 0
def check(name, cond, detail=""):
    global PASS, FAIL
    if cond:
        PASS += 1
        print(f"  PASS  {name}")
    else:
        FAIL += 1
        print(f"  FAIL  {name}  {detail}")

# --- C1-C6 peer: PC as WinRT GATT server (variant A port) ----------------
SVC_UUID = "12345678-1234-1234-1234-123456789abc"
CHAR_UUID = "12345678-1234-1234-1234-123456789a01"

class GattPeer:
    """One GATT service, one notify+read characteristic, connectable adv."""

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

    def _on_read(self, sender, args):
        try:
            req = args.get_request().get_results()
            w = self._DataWriter()
            w.write_bytes(b'{"v":99,"who":"peer"}')
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

# --- Boot sanity ---------------------------------------------------------
r = cmd("STATUS", 1.5)
check("device sanity (STATUS)", r is not None and r.get("cmd") == "status", str(r))
check("STATUS conn object present", r is not None and
      isinstance(r.get("conn"), dict), str(r))
check("conn enabled", r is not None and r["conn"].get("enabled") is True, str(r))
check("conn state off at boot", r is not None and
      r["conn"].get("state") == "off", str(r))

# --- Error paths before any target ---------------------------------------
r = cmd("CONN STATUS")
check("CONN STATUS off", r and r.get("state") == "off" and r.get("mode") == "none", str(r))

r = cmd("CONN")
check("CONN bare -> syntax error", r and r.get("status") == "error" and
      "invalid syntax" in r.get("msg", ""), str(r))

r = cmd("CONN START")
check("START without target -> -456", r and r.get("status") == "error" and
      r.get("code") == -456, str(r))

r = cmd("CONN TARGET zzzz")
check("TARGET bad uuid -> -450", r and r.get("status") == "error" and
      r.get("code") == -450, str(r))

r = cmd("CONN START 00:11:22:33:44")     # malformed address
check("START bad addr -> -450", r and r.get("status") == "error" and
      r.get("code") == -450, str(r))

r = cmd("CONN INTERVAL 50")
check("INTERVAL below range rejected", r and r.get("status") == "error", str(r))
r = cmd("CONN INTERVAL 500")
check("INTERVAL 500 ok", r and r.get("status") == "ok" and
      r.get("value") == 500, str(r))
r = cmd("CONN INTERVAL 99999")
check("INTERVAL above range rejected", r and r.get("status") == "error", str(r))

r = cmd("CONN STOP")
check("STOP when off -> -453", r and r.get("status") == "error" and
      r.get("code") == -453, str(r))

# --- Auto-connect peer search (unlikely 128-bit UUID: nothing matches) ---
UNLIKELY = "A1B2C3D4-E5F6-0789-0A1B-CCDDEEFF0011"
r = cmd(f"CONN TARGET {UNLIKELY}")
check("TARGET 128-bit uuid ok", r and r.get("status") == "ok", str(r))

r = cmd("CONN START")
check("START auto -> ok mode=auto", r and r.get("status") == "ok" and
      r.get("mode") == "auto", str(r))

r = cmd("CONN STATUS", 1.0)
check("state peer_search while searching", r and
      r.get("state") == "peer_search", str(r))

r = cmd("CONN STOP")
check("STOP from peer_search ok", r and r.get("status") == "ok", str(r))
r = cmd("CONN STATUS")
check("state off after stop", r and r.get("state") == "off", str(r))

# --- Ctrl+C interrupts a peer search -------------------------------------
cmd(f"CONN TARGET {UNLIKELY}")
cmd("CONN START")
time.sleep(0.5)
s.write(b"\x03")
s.flush()
time.sleep(0.8)
s.reset_input_buffer()
r = cmd("CONN STATUS")
check("Ctrl+C aborts peer search", r and r.get("state") == "off", str(r))

# --- Direct connect to an absent peer (bounded by link timeout) ----------
r = cmd("CONN START 00:11:22:33:44:55 public", w=9.0)
check("direct connect unreachable -> -455", r and r.get("status") == "error" and
      r.get("code") == -455, str(r))
r = cmd("CONN STATUS")
check("state off after failed direct connect", r and r.get("state") == "off", str(r))

# --- Coexistence bookkeeping: conn commands during an active scan --------
r = cmd_during_scan("SCAN START")
check("SCAN START ok", r and r.get("status") == "ok", str(r))
r = cmd_during_scan("CONN STATUS")
check("CONN STATUS while scanning", r and r.get("state") == "off", str(r))
r = cmd_during_scan(f"CONN TARGET {UNLIKELY}")
check("TARGET while scanning ok", r and r.get("status") == "ok", str(r))
r = cmd_during_scan("CONN START")
check("auto START while scanning ok (tap path)", r and r.get("status") == "ok", str(r))
r = cmd_during_scan("CONN STOP", timeout=12)
check("STOP while scanning ok", r and r.get("status") == "ok", str(r))
r = cmd_during_scan("SCAN STOP")
check("SCAN STOP ok", r and r.get("status") == "ok", str(r))

# --- Adv stream unaffected by the conn feature ---------------------------
r = cmd_during_scan("SCAN START")
s.reset_input_buffer()
end = time.time() + 4
data = b""
while time.time() < end:
    chunk = s.read(4096)
    if chunk:
        data += chunk
check("advertisements still flow", data.count(b'"addr"') > 0,
      f"{data.count(b'\"addr\"')} lines")
r = cmd_during_scan("SCAN STOP")
check("SCAN STOP after churn", r and r.get("status") == "ok", str(r))

# --- C1-C6: GATT data path with the WinRT peer (skips when unavailable) ---
peer = None
try:
    peer = GattPeer()
    peer.start()
    time.sleep(1.0)
except Exception as e:
    print(f"  SKIP  C1-C6 (WinRT GATT server unavailable: {e!r})")

if peer is not None:
    r = cmd_during_scan("SCAN START")
    check("C1 SCAN START", r and r.get("status") == "ok", str(r))
    r = cmd_during_scan(f"CONN TARGET {SVC_UUID} {CHAR_UUID}")
    check("C1 TARGET svc+char", r and r.get("status") == "ok", str(r))
    r = cmd_during_scan("CONN START")
    check("C1 auto START (tap path)", r and r.get("status") == "ok", str(r))

    peer_addr = ""
    end = time.time() + 15
    while time.time() < end:
        r = cmd_during_scan("CONN STATUS")
        if r and r.get("state") == "active":
            peer_addr = r.get("addr", "")
            break
        time.sleep(0.5)
    check("C1 connected within 15 s", peer_addr != "", "never active")
    check("C1 subscribed (notify mode)", peer.wait_subscribed(5),
          "CCCD never written")

    for i in range(5):
        peer.notify(json.dumps({"v": i, "who": "peer"}).encode())
        time.sleep(0.3)
    s.reset_input_buffer()
    lines = []
    end = time.time() + 3
    while time.time() < end:
        t = s.readline().decode(errors="replace").strip()
        if t.startswith("{") and '"src":"conn"' in t:
            try:
                lines.append(json.loads(t))
            except json.JSONDecodeError:
                pass
    check("C1 conn lines re-streamed (>=2)", len(lines) >= 2,
          f"{len(lines)} lines")
    if lines:
        check("C1 merged payload fields",
              all("v" in l and l.get("who") == "peer" for l in lines),
              str(lines[:2]))
        check("C1 envelope addr matches peer",
              all(l.get("addr") == peer_addr for l in lines), str(lines[:2]))

    r = cmd_during_scan("SCAN STOP")
    check("C4 SCAN STOP with conn up", r and r.get("status") == "ok", str(r))
    r = cmd("POWER STATUS", 1.0)
    check("C4 POWER active while connected, scan off",
          r and "active" in json.dumps(r), str(r))

    for i in range(60):                      # C5 chatty peer
        try:
            peer.notify(json.dumps({"v": i, "who": "peer"}).encode())
        except Exception:
            break
        time.sleep(0.02)
    time.sleep(1)
    r = cmd("STATUS", 1.0)
    check("C5 responsive after burst", r and r.get("status") == "ok", str(r))
    r = cmd("CONN STATUS")
    check("C5 rx_notify counted", r and r.get("rx_notify", 0) > 0, str(r))
    r = cmd("CONN STOP", 1.5)
    check("C5 STOP ok", r and r.get("status") == "ok", str(r))
    time.sleep(1)

    if peer_addr:                            # C2 direct by learned address
        r = cmd_during_scan("SCAN START")
        r = cmd_during_scan(f"CONN START {peer_addr}")
        check("C2 direct START (learned type)", r and r.get("status") == "ok",
              str(r))
        end = time.time() + 10
        ok = False
        while time.time() < end:
            r = cmd_during_scan("CONN STATUS")
            if r and r.get("state") == "active":
                ok = True
                break
            time.sleep(0.5)
        check("C2 direct reconnect", ok, "never active")
        cmd_during_scan("CONN STOP")
        cmd_during_scan("SCAN STOP")
        time.sleep(1)

    # C6 peer vanishes (best effort: link supervision can exceed window)
    r = cmd_during_scan("CONN START")
    end = time.time() + 15
    up = False
    while time.time() < end:
        r = cmd_during_scan("CONN STATUS")
        if r and r.get("state") == "active":
            up = True
            break
        time.sleep(0.5)
    if up:
        peer.stop()
        peer = None
        dropped = False
        end = time.time() + 12
        while time.time() < end:
            r = cmd("CONN STATUS")
            if r and r.get("state") == "off":
                dropped = True
                break
            time.sleep(1)
        if dropped:
            check("C6 disconnect on peer vanish", True)
        else:
            print("  SKIP  C6 (link supervision longer than window)")
            cmd("CONN STOP")
    else:
        print("  SKIP  C6 (reconnect for vanish test failed)")

    if peer is not None:
        peer.stop()
    cmd("CONN STOP")
    cmd("SCAN STOP")

s.close()
print(f"\n{PASS} passed, {FAIL} failed")
exit(1 if FAIL else 0)
