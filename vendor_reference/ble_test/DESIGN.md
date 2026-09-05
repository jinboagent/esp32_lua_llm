# DESIGN — Windows BLE Peripheral Test Tool

Implementation-ready design and function-reference document. Written for an
**AI coding agent** that will maintain or extend this project. Everything in
the "Verified facts" sections was confirmed empirically on the target machine
(2026-08, see §2.2) — treat those as ground truth, not guesses.

Companion files: `ble_peripheral_test.py` (implementation), `verify_scan.py`
(companion scanner), `README.md` (user guide).

---

## 1. Purpose and scope

Answer one question with evidence: **can this Windows PC's Bluetooth adapter
operate in BLE peripheral mode?** — at three levels:

| Level | Mechanism | What PASS proves |
|---|---|---|
| A. Capability claim | `BluetoothAdapter.IsPeripheralRoleSupported` | driver/radio declares peripheral support |
| B. Broadcast (beacon) | `BluetoothLEAdvertisementPublisher` | radio actually transmits an advertisement (non-connectable) |
| C. Connectable GATT server | `GattServiceProvider` + connectable advertisement | PC can accept a LE connection and serve GATT (read/write/notify) |

Out of scope: classic Bluetooth, bonding/pairing UX, cross-platform support
(Windows-only; see §14.5 for a Linux/bleak port path).

---

## 2. Environment and verified status

### 2.1 Runtime environment (pin these)

- OS: Windows 10 22H2 (10.0.19045), Chinese locale console — **all script
  output must stay ASCII**; `main()` reconfigures stdout to UTF-8 with
  `errors="replace"` as a safety net.
- Interpreter: `python` = **Python 3.12.10**. The `winrt-*` packages are
  installed ONLY in 3.12 (`py` = 3.14 has none). All docs/commands use `python`.
- Packages (already installed, do not reinstall):
  - `winrt-runtime` 3.2.1
  - `winrt-Windows.Devices.Bluetooth` 3.2.1
  - `winrt-Windows.Devices.Bluetooth.Advertisement` 3.2.1
  - `winrt-Windows.Devices.Bluetooth.GenericAttributeProfile` 3.2.1
  - `winrt-Windows.Devices.Radios` 3.2.1
  - `winrt-Windows.Storage.Streams` 3.2.1
  - `bleak` 3.0.2 (used only by `verify_scan.py`)
- Hardware: MediaTek Bluetooth Adapter (MAC `CC:47:40:8F:15:75`).

### 2.2 Verified results on the target machine

- `is_peripheral_role_supported = True`
- Beacon publisher reached `STARTED` in ~0.2 s (manufacturer-data payload).
- `GattServiceProvider.create_async` → `SUCCESS`, characteristic created,
  connectable advertisement reached `STARTED` — **from plain desktop Python,
  no MSIX/package identity needed on this machine**. The package-identity
  block described in §12 is a *possible* failure mode, not the observed one.

### 2.3 Import surface (exact, working)

```python
from winrt.windows.devices.bluetooth import BluetoothAdapter, BluetoothError
from winrt.windows.devices.bluetooth.advertisement import (
    BluetoothLEAdvertisement,
    BluetoothLEAdvertisementPublisher,
    BluetoothLEAdvertisementPublisherStatus,
    BluetoothLEManufacturerData,
)
from winrt.windows.devices.bluetooth.genericattributeprofile import (
    GattCharacteristicProperties,
    GattLocalCharacteristicParameters,
    GattProtectionLevel,
    GattServiceProvider,
    GattServiceProviderAdvertisementStatus,
    GattServiceProviderAdvertisingParameters,
)
from winrt.windows.devices.radios import Radio, RadioKind, RadioState
from winrt.windows.storage.streams import DataWriter, DataReader  # DataReader only for from_buffer
```

Note: there is **no** `GattServiceProviderCreationStatus` in this projection;
creation results are judged via `GattServiceProviderResult.error` (a
`BluetoothError`). `GattServiceProvider` has **no `.advertisement` property**
in this SDK projection — do not try to set a local name on the GATT payload.

---

## 3. Architecture

```
ble_peripheral_test.py (single-file CLI, asyncio)
|
|-- Phase A  phase_adapter()            BluetoothAdapter + Radio enumeration
|-- Phase B  phase_beacon(seconds)      BluetoothLEAdvertisementPublisher
|-- Phase C  phase_gatt(seconds)        GattServiceProvider (+ serve mode, seconds==0)
|-- Phase D  verdict in main()          summary dict -> report + exit code
|
+-- shared helpers: log, bytes_to_buffer, buffer_to_bytes, decode_error
    (WinRT event handlers run on threadpool threads -> dispatched back to the
     asyncio loop via run_coroutine_threadsafe; see §7)

verify_scan.py (separate machine)  bleak BleakScanner detection callback
```

Control flow: `main()` runs phases sequentially, merges their returned summary
dicts (§9), prints the verdict, returns the process exit code (§10).

Data flow: WinRT events → small sync callbacks → shared `state` dict → polled
by the asyncio main loop every 250 ms. Request-style events (read/write) use
deferrals and cross-thread coroutine dispatch (§7.2).

---

## 4. Domain constants (do not change casually — phones/READMEs reference them)

```python
COMPANY_ID = 0xFFFF                     # reserved-for-testing manufacturer id
SERVICE_UUID = 34a1b001-7c6d-4e8f-9a2b-3d4e5f607182   # custom GATT service
CHAR_UUID    = 34a1b002-7c6d-4e8f-9a2b-3d4e5f607182   # read|write|notify char
BEACON manufacturer payload: b"PERIPH"  # unnamed beacon, 6 bytes
GATT read response value:    f"hello #{n}".encode()
GATT notification value:     f"notify #{n}".encode()
```

All UUIDs are `uuid.UUID` objects (pywinrt converts them to WinRT GUIDs
automatically when passed to async factories).

Polling cadence: 250 ms status poll; notification push every 3.0 s while ≥1
subscribed client; heartbeat every 5.0 s while advertising and no central has
interacted.

---

## 5. Function reference — `ble_peripheral_test.py`

Every function below is implemented and passing on the target machine.
Signatures are the contract; keep them stable when refactoring.

### 5.1 Module-level helpers

```python
def log(msg: str) -> None
```
- Prints immediately (`flush=True`); single funnel for all output.

```python
def bytes_to_buffer(data: bytes):
```
- `DataWriter().write_bytes(bytes(data))` then `detach_buffer()` → WinRT
  `IBuffer`. Used for manufacturer payloads, static values, read responses,
  notification values.

```python
def buffer_to_bytes(buffer) -> bytes:
```
- `bytes(memoryview(buffer)[: buffer.length])`. This is the ONLY reliable
  IBuffer→bytes path in pywinrt 3.2.1 (§6.2). Truncates to `buffer.length`
  because `memoryview` exposes capacity.

```python
def decode_error(error) -> str
```
- Maps a `BluetoothError` enum (or `None` → `"none"`) to its member name via
  prebuilt `BLUETOOTH_ERROR_NAMES` dict. `BluetoothError` members confirmed:
  `SUCCESS, OTHER_ERROR, RADIO_NOT_AVAILABLE, DEVICE_NOT_CONNECTED,
  DISABLED_BY_USER, DISABLED_BY_POLICY, NOT_SUPPORTED, TRANSPORT_NOT_SUPPORTED,
  CONSENT_REQUIRED, RESOURCE_IN_USE`.

### 5.2 `phase_adapter() -> dict`

```python
async def phase_adapter() -> dict   # returns {"peripheral_supported": bool}
```
- Calls `await BluetoothAdapter.get_default_async()`; returns
  `{"peripheral_supported": False}` if adapter is `None`.
- Prints adapter MAC formatted from `bluetooth_address` (64-bit int → six
  hex pairs), and the four capability booleans:
  `is_low_energy_supported`, `is_central_role_supported`,
  `is_peripheral_role_supported`, `is_advertisement_offload_supported`.
- Then `await Radio.get_radios_async()`, printing each radio's name, `int(kind)`,
  decoded state. Enum ints observed: RadioKind 1=WI_FI, 3=BLUETOOTH;
  RadioState 1=ON. Comparison `radio.kind == RadioKind.BLUETOOTH` works
  (enums are int subclasses) — note `print(radio.kind)` shows a bare int.

### 5.3 `phase_beacon(seconds: float) -> dict`

```python
async def phase_beacon(seconds: float) -> dict   # returns {"beacon_pass": bool}
```
- Builds `BluetoothLEAdvertisement` with **manufacturer data only**
  (`0xFFFF`/`b"PERIPH"`). **Golden rules**: never set `local_name` (this stack
  rejects it, §6.3), never set `flags` (WinRT rejects), total payload ≤ 31 bytes.
- Constructs `BluetoothLEAdvertisementPublisher(advertisement)`, subscribes
  `add_status_changed(on_status_changed)`. The handler is a **sync callback**
  storing `(int(args.status), args.error)` into `state`.
- `publisher.start()` wrapped in `try/except OSError` → reports
  `[FAIL] ... HRESULT` and returns `{"beacon_pass": False}` instead of crashing.
- Poll loop: 250 ms; prints each status transition; success = observing
  `STARTED` (loop breaks ~1.5 s after first `STARTED`, or at `seconds`).
  `ABORTED` prints the decoded error plus a hint mapping
  (RADIO_NOT_AVAILABLE / DISABLED_BY_POLICY / NOT_SUPPORTED).
- `finally`: `publisher.stop()`, `remove_status_changed(token)`.

### 5.4 `phase_gatt(seconds: float) -> dict`

```python
async def phase_gatt(seconds: float) -> dict
# returns {"gatt_result": "PASS"|"FAIL"|"BLOCKED", "client_connected": bool}
```
Largest unit. Stages (each early-returns `BLOCKED`/`FAIL` with an explanation
instead of raising):

1. **Create provider** — `await GattServiceProvider.create_async(SERVICE_UUID)`.
   OSError from the call, or `result.service_provider is None`, or
   `int(result.error) != int(BluetoothError.SUCCESS)` → `BLOCKED` + print
   `GATT_BLOCKED_HINT` (package-identity explanation, §12).
2. **Create characteristic** — `GattLocalCharacteristicParameters()` with
   `characteristic_properties = READ | WRITE | NOTIFY`
   (enum OR works — int subclass), `read_protection_level` and
   `write_protection_level = GattProtectionLevel.PLAIN`,
   `static_value = bytes_to_buffer(b"WinBLE-GATT ready")`,
   `user_description = "Test read/write/notify characteristic"`.
   Then `await provider.service.create_characteristic_async(CHAR_UUID, params)`;
   `.characteristic is None` or error → `FAIL`.
3. **Wire event handlers** (see §7) — read/write requested, subscribed-clients
   changed. `note_central_activity()` increments `state["interactions"]` and
   prints the one-time banner `*** a central is connected and interacting ***`,
   flipping `state["connected"]` (this is the ONLY connection detector —
   WinRT exposes no direct "central connected" event on GattServiceProvider).
4. **Advertise** — `GattServiceProviderAdvertisingParameters()` with
   `is_connectable = True`, `is_discoverable = True`;
   `provider.start_advertising_with_parameters(adv_params)` in `try/except
   OSError` → `BLOCKED` on failure. Prints the phone walkthrough.
5. **Serve loop** — `while seconds == 0 or elapsed < seconds:` (`0` = serve
   until Ctrl+C): 250 ms tick, print status transitions (`STARTED` or
   `STARTED_WITHOUT_ALL_ADVERTISEMENT_DATA` → verdict `PASS`; `ABORTED` →
   `BLOCKED` + break), heartbeat every 5 s while no central interacted,
   notification push every 3 s while `len(list(characteristic.subscribed_clients)) > 0`.
   `finally`: `provider.stop_advertising()` + remove all event tokens.
6. **Received-data summary + return** — after the loop, all writes received
   from the central are printed (`Data received from the central (N writes)`
   with `[seconds] bytes` per line, or the "No data" line). Returns
   `{"gatt_result": verdict, "client_connected": state["connected"],
   "received_count": len(state["received"])}`.

Inner functions (closures over `characteristic`/`loop`/`state`) — keep as a
unit when refactoring:

| Closure | Contract |
|---|---|
| `note_central_activity()` | sync; mutates `state`; one-time connect banner |
| `handle_read(args)` | async; `req = await args.get_request_async()`; responds `respond_with_value(bytes_to_buffer(f"hello #{n}"))` |
| `handle_write(args)` | async; logs `buffer_to_bytes(request.value)`; appends `(seconds, bytes)` to `state["received"]`; `request.respond()` |
| `dispatch(args, coroutine)` | sync; `deferral = args.get_deferral()`; `run_coroutine_threadsafe(coro, loop)`; done-callback logs errors then `deferral.complete()` |
| `on_read_requested / on_write_requested(sender, args)` | sync; `note_central_activity()` then `dispatch(args, handler(args))` |
| `on_subscribed_clients_changed(sender, args)` | sync; logs subscribed count via `len(list(characteristic.subscribed_clients))` |
| `on_adv_status_changed(sender, args)` | sync; stores `int(args.status)`, `args.error` into `state` |

### 5.5 `main() -> int`

- argparse (§10); reconfigures stdout to UTF-8/replace when not already UTF-8.
- Runs A (always), B unless `--skip-beacon`, C unless `--skip-gatt`; merges
  summary dicts; prints verdict block; computes exit code (§10).

---

## 6. pywinrt 3.2.1 binding reference (empirical)

### 6.1 Naming conventions
- Enum members: `SCREAMING_SNAKE_CASE` (`RadioState.ON`,
  `GattServiceProviderAdvertisementStatus.STARTED_WITHOUT_ALL_ADVERTISEMENT_DATA`).
- Properties/methods: `snake_case` (`is_connectable`, `notify_value_async`).
- Events: `add_<event>(handler)` / `remove_<event>(token)`; handler receives
  `(sender, args)`.
- WinRT async ops (`create_async`, `get_default_async`, `get_request_async`,
  …) are directly `await`able inside asyncio.

### 6.2 Buffer handling (critical)
- `IBuffer` supports the buffer protocol: **read** via
  `bytes(memoryview(buf)[: buf.length])`; **write** via
  `DataWriter.write_bytes()` + `detach_buffer()` (or `Buffer(n)` +
  `memoryview` slice assignment, as bleak does).
- `DataReader.read_bytes(n)` is NOT usable as `read_bytes(count) -> bytes`;
  it expects a preallocated array argument and misbehaves — avoid it.
  `DataReader.from_buffer(buf)` exists; the memoryview route is simpler.

### 6.3 Platform quirks with exact error codes

| Fact | Evidence |
|---|---|
| Publisher adv with `local_name` set → `OSError 0x80070057` E_INVALIDARG (MediaTek stack) | isolated experiment: name-only and name+mfg both fail; mfg-only starts |
| Publisher adv payload must be non-empty → `0x8007000D` | empty-adv experiment |
| Total adv payload ≤ 31 bytes (legacy AD limit) | E_INVALIDARG when name+mfg exceeded it; shortened payloads work |
| `GattServiceProvider.advertisement` property does not exist in this projection | `dir()` + AttributeError at runtime |
| `GattServiceProviderCreationStatus` does not exist | ImportError |
| Read/write event args expose the request via `await args.get_request_async()`, NOT a `.request` property | `dir(GattReadRequestedEventArgs)` |
| `radio.kind` prints as bare int (enums are int subclasses) | radio enumeration output |
| Advertisement with no local name → phone sees an unnamed device; identify by service UUID / manufacturer data | user verification flow §13 |

### 6.4 Enum int values (observed)
- `BluetoothLEAdvertisementPublisherStatus`: CREATED=0, WAITING=1, STARTED=2,
  STOPPING=3, STOPPED=4, ABORTED=5 (status 2 confirmed on air).
- `GattServiceProviderAdvertisementStatus`: CREATED=0, STARTED=1,
  STARTED_WITHOUT_ALL_ADVERTISEMENT_DATA=2, ABORTED=3, STOPPED=4 (STARTED=1
  confirmed on this machine).
- `RadioState`: UNKNOWN=0, ON=1, OFF=2, DISABLED=3. `RadioKind`:
  OTHER=0?, WI_FI=1, FM=2, BLUETOOTH=3 (WI_FI=1 and BLUETOOTH=3 observed).

---

## 7. Concurrency model

### 7.1 Status events (fire-and-forget)
WinRT raises `status_changed` / `advertisement_status_changed` on threadpool
threads. Handlers are tiny **sync** functions that only write into the shared
`state` dict (GIL-safe). The asyncio loop polls `state` at 250 ms and prints
transitions. Rationale: avoids marshaling, and pywinrt sync handlers cannot
`await`.

### 7.2 GATT requests (must respond)
Read/write requests use the **deferral pattern**, required because the actual
request must be fetched asynchronously:

```
WinRT threadpool thread:
  on_write_requested(sender, args)
    -> note_central_activity()            # sync bookkeeping
    -> dispatch(args, handle_write(args))
         deferral = args.get_deferral()   # hold the request open
         run_coroutine_threadsafe(handle_write(args), loop)
             .add_done_callback(log-then-deferral.complete())

asyncio loop thread:
  handle_write(args):
    request = await args.get_request_async()
    ...log / respond / respond_with_value...
```

Rules for extenders:
- Always pair `get_deferral()` with `complete()` in a `finally`.
- Never call `await` inside the sync handler; always
  `asyncio.run_coroutine_threadsafe(coro, loop)` with `loop` captured from
  `asyncio.get_running_loop()` while the loop runs.
- Keep strong references to the Python handler functions and event tokens
  (locals suffice — they live for the whole phase).

---

## 8. State schemas

`state` dict inside `phase_gatt` (shared across threads):

```python
{
  "status": int | None,        # latest GattServiceProviderAdvertisementStatus
  "error":  BluetoothError | None,
  "printed": int | None,       # last status already logged (transition dedup)
  "connected": bool,           # flipped on first read/write/subscribe event
  "interactions": int,         # count of central activities
  "received": list[tuple[float, bytes]],  # (seconds since phase start, written bytes) per write
}
```

`state` in `phase_beacon`: `{"status", "error", "printed"}` + the
`on_status_changed` handler ref.

Merged `summary` dict consumed by the verdict block:

```python
{
  "peripheral_supported": bool,   # from Phase A
  "beacon_pass": bool,            # from Phase B (absent if skipped)
  "gatt_result": str,             # "PASS" | "FAIL" | "BLOCKED" (absent if skipped)
  "client_connected": bool,       # from Phase C
  "received_count": int,          # from Phase C: writes received from the central
}
```

---

## 9. Output contract

Phases print `[PASS]` / `[FAIL]` / `[BLOCKED]` / `[GATT]` / `[WARN]` prefixed
lines; the verdict block lists exactly:

```
Adapter supports BLE peripheral role : PASS|FAIL
Beacon advertisement over the air    : PASS|FAIL|SKIPPED
Connectable GATT server advertising  : PASS (advertisement only, no client needed)|FAIL|BLOCKED|SKIPPED
Central connected during the run     : YES|NO (nothing connected; connect with nRF Connect)   [only when Phase C ran]
Writes received from the phone       : N                                                          [only when Phase C ran]
```

Semantics (keep!): Phase C `PASS` means the *connectable advertisement
started* — it explicitly does NOT imply a client connected; connection
evidence is the separate `Central connected` line. Consumers (humans, CI,
agents) rely on this wording.

---

## 10. CLI contract

```
python ble_peripheral_test.py [--adv-seconds FLOAT] [--gatt-seconds FLOAT]
                              [--skip-beacon] [--skip-gatt]
```
- `--adv-seconds` default 15.0 (max beacon test window; breaks early ~1.5 s
  after `STARTED`).
- `--gatt-seconds` default 60.0; **0 = serve mode** (advertise until Ctrl+C).
- Exit codes: `0` = peripheral role supported AND (beacon PASS, or beacon
  skipped AND GATT PASS); `1` = otherwise; `130` = Ctrl+C.

```
python verify_scan.py [--seconds FLOAT] [--name SUBSTRING]
```
- Reports devices whose advertisement contains manufacturer id `0xFFFF`, or a
  service UUID starting `34a1b0`, or (with `--name`) a name substring.
  Detection callback prints `[sighting] name addr rssi manufacturer services`.

---

## 11. Acceptance tests (run after any change)

On the target machine, from `E:\agent\test`:

| Command | Must show | Exit |
|---|---|---|
| `python ble_peripheral_test.py --skip-beacon --gatt-seconds 8` | Phase A PASS line for peripheral role; Phase C `advertisement status -> STARTED`; heartbeat lines; verdict `Connectable GATT server advertising  : PASS`; `Central connected ... : NO` | 0 |
| `python ble_peripheral_test.py --adv-seconds 8 --skip-gatt` | `publisher status -> STARTED` within ~1 s | 0 |
| `python ble_peripheral_test.py --skip-beacon --gatt-seconds 0` + phone nRF Connect connect/read/write/subscribe (§13), then Ctrl+C | banner `*** a central is connected and interacting ***`, `[GATT] read/write/subscribed` lines, `notify #N` every 3 s, verdict `Central connected ... : YES` | 0 / 130 |
| `python verify_scan.py --seconds 5` | starts, prints scan banner, exits cleanly (finds nothing on the same machine — expected) | 0 |

Regression traps: setting `local_name` on the beacon (E_INVALIDARG), payload
> 31 bytes, missing deferral `complete()`, awaiting inside sync handlers,
`python` vs `py` interpreter mixup.

---

## 12. Failure mode: Windows package-identity block

`GattServiceProvider` is documented as requiring an app with **package
identity** + the `bluetooth` capability (UWP permission model; MSIX/sparse
package is the workaround). This machine did NOT enforce it. If Phase C ever
prints `BLOCKED`:
1. Report it as a Windows policy result (not hardware).
2. Workarounds: MSIX-packaged app with `bluetooth` capability; sparse package
   identity for an unpackaged exe; or move the peripheral to Linux/BlueZ
   (`bleak.BleakServer`) / external dongle (ESP32, nRF52).

---

## 13. Over-the-air verification (phone)

1. Laptop: `python ble_peripheral_test.py --skip-beacon --gatt-seconds 0`,
   wait for `[PASS] Connectable GATT server is advertising.`
2. Phone: nRF Connect for Mobile → SCAN. Entry is **unnamed** (stack rejects
   local names): sort by RSSI next to the PC; check for advertised service
   UUID `34a1b001-...`; running without `--skip-beacon` also adds `0xFFFF`
   manufacturer data to the same device.
3. CONNECT → expand service `34a1b001-...` → characteristic `34a1b002-...`:
   read (→ `hello #N`), write bytes (logged), enable notifications
   (→ `notify #N` every 3 s).
4. A Windows adapter cannot see its own advertisement — verify from a second
   device only (`verify_scan.py` or the phone).

---

## 14. Extension roadmap (for the implementing agent)

1. **More characteristics/services** — repeat the §5.4 stage-2/3 pattern per
   characteristic; remember each needs its own UUID and handlers.
2. **Reliable connection/disconnection events** — from
   `GattReadRequestedEventArgs.Session` / `GattWriteRequestedEventArgs.Session`
   obtain the `GattSession` and subscribe `add_session_status_changed`
   (`ACTIVE`/`CLOSED`). Would replace the heuristic
   `note_central_activity()` banner.
3. **Periodic advertisement payload rotation** (e.g. sensor-style beacon):
   stop/reconfigure/start the publisher; re-check the 31-byte budget.
4. **Bonding/pairing** — set `read_protection_level` /
   `write_protection_level` to `ENCRYPTED`* variants; document the pairing
   prompt implications.
5. **Linux port** — `bleak` `BleakServer` (BlueZ) exposes a true connectable
   peripheral; keep the CLI/exit-code contract identical.
6. **CI smoke test** — Phase A only (`--skip-beacon --skip-gatt`) asserts
   `peripheral_supported` on hardware-bearing runners.

## 15. AI-agent onboarding checklist

1. Read §6 (binding quirks) and §7 (threading) before touching event code.
2. Verify environment: `python --version` → 3.12.x;
   `pip list | grep winrt` → 3.2.1 present.
3. Run the two fast acceptance commands in §11 (≤ 20 s each).
4. Preserve: function signatures (§5), summary-dict keys (§8), output/exit
   contracts (§9–10), the golden rules in §5.3/§6.3.
5. New WinRT calls: trust `dir()`/small experiments over memory — this
   projection deviates from C# docs in places (§6.3).
6. Keep output ASCII-only; never print emoji/box-drawing in phase logs.
