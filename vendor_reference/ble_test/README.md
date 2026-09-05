# Windows BLE Peripheral Capability Test

Python test programs that answer: **can this Windows laptop use its Bluetooth
adapter in BLE peripheral mode?** — including a real over-the-air beacon and a
connectable GATT server with read/write/notify.

## Result on this laptop (observed)

| Check | Result |
|---|---|
| `BluetoothAdapter.IsPeripheralRoleSupported` | **True** (MediaTek adapter) |
| Beacon advertisement reaches `STARTED` | **PASS** (in ~0.2 s) |
| `GattServiceProvider` + connectable advertisement | **PASS** — full GATT server works from desktop Python here |

So this laptop **can** act as a BLE peripheral, including a connectable one.

## Files

| File | Purpose |
|---|---|
| `ble_peripheral_test.py` | Main test: adapter capability report, beacon advertisement, connectable GATT server probe |
| `verify_scan.py` | Companion scanner (run on a **second** PC) that detects the test advertisement over the air |
| `DESIGN.md` | Full design + function reference + platform quirks — **start here if you are an AI agent extending this code** |

## Requirements

- Windows 10/11 with a Bluetooth adapter
- Python 3.12 with the `winrt-*` packages (`winrt-Windows.Devices.Bluetooth`,
  `...Bluetooth.Advertisement`, `...Bluetooth.GenericAttributeProfile`,
  `...Radios`, `...Storage.Streams`, `...Foundation`) — already installed in
  this project's Python 3.12 environment (`python`, not `py`).
- `bleak` only for `verify_scan.py` (already installed).

## Run

```
python ble_peripheral_test.py                     # all phases
python ble_peripheral_test.py --adv-seconds 8 --gatt-seconds 10   # quick run
python ble_peripheral_test.py --skip-beacon       # only adapter + GATT server
python ble_peripheral_test.py --skip-beacon --gatt-seconds 0      # SERVE MODE
```

`--gatt-seconds 0` keeps the GATT server advertising **until Ctrl+C**, so you
have time to connect from your phone; every read/write/notification from the
central is logged, and a heartbeat shows it is still waiting.

Exit code `0` means the adapter supports the peripheral role and at least the
beacon advertisement worked.

## What each phase means

### Phase A — adapter capability report
`BluetoothAdapter.IsPeripheralRoleSupported` reports whether the driver/radio
can act as a BLE peripheral. This is the direct hardware-level answer.

### Phase B — beacon advertisement (non-connectable peripheral)
Uses `BluetoothLEAdvertisementPublisher` to broadcast manufacturer data
`0xFFFF = b'PERIPH'`. If the status reaches `STARTED`, the radio is genuinely
transmitting as a peripheral.

Quirks found on this stack (MediaTek / Win10 22H2):
- `local_name` in a publisher payload is rejected with `E_INVALIDARG`
  (0x80070057) — so the beacon is **unnamed**; identify it by manufacturer data.
- The whole advertisement payload must stay within the 31-byte legacy limit.

### Phase C — connectable GATT server
Uses `GattServiceProvider` to publish service
`34a1b001-7c6d-4e8f-9a2b-3d4e5f607182` with one characteristic
`34a1b002-7c6d-4e8f-9a2b-3d4e5f607182` (read / write / notify), then advertises
it as **connectable** (`is_connectable=True`).

**Reading the result:** `PASS` means the connectable advertisement started —
that alone answers "can this laptop act as a connectable peripheral". It does
**not** mean a client connected; the final verdict has a separate
"Central connected during the run" line that only turns YES after a phone/PC
actually interacts (reads, writes, or subscribes are logged live).

Writes from a central are printed in the script log; reads are answered with a
`hello #N` value; while a central has notifications enabled, a `notify #N`
counter is pushed every 3 s.

**Caveat:** Microsoft restricts `GattServiceProvider` for apps without
*package identity* + the `bluetooth` capability. It worked here from plain
desktop Python, but on other machines/managed systems it may return
`BLOCKED` — that is a Windows policy result, not a hardware limit. Workarounds:

1. Give the app package identity — MSIX packaging or a *sparse package* with an
   external manifest declaring the `bluetooth` capability.
2. Run the peripheral on Linux, where `bleak.BleakServer` implements a full
   GATT server over BlueZ.
3. Use an external BLE board (ESP32, nRF52, Raspberry Pi).

## Verifying over the air

A Bluetooth adapter generally **cannot see its own advertisements**, so verify
from another device.

### Connecting with nRF Connect on your phone (full walkthrough)

1. On the laptop: `python ble_peripheral_test.py --skip-beacon --gatt-seconds 0`
   (serve mode — runs until Ctrl+C). Wait for the line
   `[PASS] Connectable GATT server is advertising.`
2. On the phone: install **nRF Connect for Mobile** (Android/iOS) and grant it
   location/Bluetooth permission (Android requires location for scanning).
3. Tap **SCAN**. The test advertisement has **no local name** (this Bluetooth
   stack rejects names in publisher payloads), so:
   - hold the phone right next to the laptop and sort by RSSI (strongest
     first), or filter to unnamed entries;
   - the correct entry shows advertised service UUID `34a1b001-...` in its
     advertisement (tap the entry / RAW view to check);
   - running without `--skip-beacon` also adds manufacturer data `0xFFFF` to
     the same device, which makes it easier to spot.
4. Tap **CONNECT** on that entry. The script prints
   `[GATT] *** a central is connected and interacting ***` as soon as you
   read/write/subscribe.
5. Expand the unknown/custom service `34a1b001-...`, then on characteristic
   `34a1b002-...`:
   - **read** (down arrow) — returns `hello #N`, logged in the script;
   - **send data to the PC** (single **up-arrow** icon) — a write dialog
     opens; pick the **Text** format (or Bytes for hex), type e.g.
     `hello from phone`, tap **SEND**. The Python script receives it
     immediately (`[GATT] write received from central: b'hello from phone'`),
     collects every write, and prints them all again in an end-of-run
     summary plus a `Writes received from the phone : N` verdict line;
   - **enable notifications** (subscribe icon) — a `notify #N` value arrives
     every 3 s in the app and is sent by the script.

### From a second PC

Run `python verify_scan.py --seconds 30` there while the main test is
advertising; it prints every sighting with RSSI (it cannot connect, only
confirm the advertisement is on air).

## References

- [BluetoothLEAdvertisementPublisher (Microsoft Learn)](https://learn.microsoft.com/en-us/uwp/api/windows.devices.bluetooth.advertisement.bluetoothleadvertisementpublisher)
- [GattServiceProvider (Microsoft Learn)](https://learn.microsoft.com/en-us/uwp/api/windows.devices.bluetooth.genericattributeprofile.gattserviceprovider)
- [WinRT APIs in desktop apps / package identity requirements](https://learn.microsoft.com/en-us/windows/apps/desktop/modernize/winrt-api-desktop-app-support)
- [bleak (BleakServer is Linux/BlueZ only)](https://github.com/hbldh/bleak/issues/81)
