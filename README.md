# ESP32-S3 BLE Sniffer Dongle

A USB dongle that passively scans BLE advertisements, parses and filters them
(with optional on-device Lua scripting), and streams structured JSON lines to
a host PC over USB CDC. A host-side LLM can analyze the captured traffic and
generate Lua scripts that are deployed back to the device — an AI-driven BLE
analysis loop.

## Status: v1.0.0 released ✅

- **All 13 features across 4 stages implemented and hardware-verified** (see `status/LATEST.md`)
- **2026-08-11 eval**: H1 (JSON escaping) / H2 (overlong-line drain) / H3 (chunk-limit doc) fixed and regression-covered; H4 (Lua pool fragmentation) remains a soak-test watch item (see `bug_check/README.md`)
- Verification on COM12 hardware: host tests **67+** · `test_bridge_hw.py` **32/32** · `test_power_hw.py` **14/14** · `test_ble_lua_hw.py` **45** · `test_ble_peer_hw.py` **11**

## Architecture

```
+------------------+        USB CDC (JSON lines)        +-------------------+
| Host PC          | <--------------------------------- | ESP32-S3 dongle   |
|                  |      commands / Lua scripts        |                   |
| LLM analyzes     | ---------------------------------> | BLE passive scan  |
| scan data and    |    (SCRIPT LOAD ... SCRIPT END)    |   -> AD parse     |
| generates Lua    |                                    |   -> C filters    |
| scripts          |                                    |   -> Lua hooks    |
+------------------+                                    |   -> JSON encode  |
                                                        +-------------------+
```

On-device data flow:

```
NimBLE scan callback -> raw ADV queue -> AD parser -> filter engine
  -> [Lua on_adv hook] -> JSON encoder -> [Lua transform hook] -> USB CDC
```

### Firmware modules (`firmware/components/`, public APIs in `interfaces/`)

| Module | Component | Role |
|--------|-----------|------|
| USB console | `usb` | USB-Serial/JTAG line I/O (IDF console, non-blocking read) |
| Storage | `storage` | LittleFS file store for Lua scripts |
| BLE | `ble` | NimBLE init, passive scan + FNV-1a dedup, pipeline task |
| Protocol | `proto`, `json_enc`, `filter` | AD parsing, JSON encoding, C filter rules |
| Lua | `lua` | Lua 5.4 on a static pool allocator, whitelist sandbox, hooks; script upload/run/stop |
| CLI | `cli` | Command parser + IDLE/SCANNING/SCRIPT_RUNNING state machine |
| Bridge | `bridge` | Text-line script upload with per-line sandbox scan |
| Power | `power` | Automatic light sleep when idle, PM activity lock while scanning |

### Design highlights

- **State machine**: CLI enforces IDLE / SCANNING / SCRIPT_RUNNING with `-911` state guards
- **Lua sandbox**: whitelist only (base/string/table/math/utf8, `string.dump` removed);
  uploads additionally scanned for `os.`/`io.`/`debug.`/`require`/etc. → reject `-612`
- **Concurrency**: `lua_State` mutex, dedup table spinlock, atomic scan state — all race findings from evaluations are fixed and regression-covered
- **Power**: tickless light sleep when idle, wake-on-USB-command; a NO_LIGHT_SLEEP
  PM lock is held while scanning (console TX drops bytes if the SoC sleeps mid-stream)

## Repository layout

```
firmware/components/   ESP-IDF components (one per module above)
interfaces/            Public C headers (*_if.h) — the only cross-module surface
main/                  app_main: init chain + USB command loop
tests/host/            Unity host tests (MinGW, no ESP-IDF needed)
harness/00-global-context/  product overview, coding rules, build env, git workflow
harness/01-features/   per-feature specs for all 13 implemented features
harness/02-future/     v2+ spec space (empty)
bug_check/             evaluations + fix reports (index: bug_check/README.md)
status/                session handoff reports (pointer: status/LATEST.md)
docs/archive/          historical plan/design documents
test_bridge_hw.py      F4.1+F4.2 hardware suite (32 checks, pyserial on COM12)
test_power_hw.py       F4.3 hardware suite (14 checks)
test_ble_lua_hw.py     BLE+Lua data-plane suite (45 checks, keeps port open)
test_ble_peer_hw.py    controlled BLE peer suite (11 checks, bleak + WinRT)
```

## Build & test

```bat
:: Host tests (PC, MinGW)
set PATH=C:\msys64\mingw64\bin;%PATH%
cmake -G "MinGW Makefiles" -S tests\host -B tests\host\build
cmake --build tests\host\build && tests\host\build\test_runner.exe

:: Firmware (ESP-IDF v5.1)
build.bat              :: full build
flash.bat              :: flash COM12
monitor.bat            :: USB-Serial/JTAG console

:: Hardware suites (device attached on COM12)
python test_bridge_hw.py
python test_power_hw.py
python test_ble_lua_hw.py COM12      :: BLE+Lua data plane (~90 s)
python test_ble_peer_hw.py COM12     :: controlled BLE peer, needs bleak+winrt (~60 s)
```

Note: after changing `sdkconfig.defaults`, delete `sdkconfig` and `build/` for a clean rebuild.

## Usage quick reference

Connect to the USB-Serial/JTAG console (COM12 @ 115200). Commands:

| Command | Effect |
|---------|--------|
| `STATUS` | state, scanning, queue_drops, filter_count, lua_ready, script_loaded/running, free_storage, pipeline stats |
| `VERSION` | firmware version (`1.0.0`) |
| `SCAN START` / `SCAN STOP` | start/stop passive scan (advertisements stream as JSON lines) |
| `SCAN INTERVAL <ms>` | set scan interval (10..10000) |
| `FILTER ADD NAME <pat>` / `UUID <hex>` / `RSSI <dBm>` / `MAC <addr>` | add filter rule (wildcards in name) |
| `FILTER LIST` / `FILTER CLEAR` | inspect / clear rules |
| `SCRIPT LOAD` → lines → `SCRIPT END` | text-line Lua upload (LLM bridge protocol) |
| `SCRIPT BEGIN/CHUNK/END` | hex-chunk upload extension (≤121 payload bytes per chunk — the USB command line fits 255 chars) |
| `SCRIPT RUN` / `SCRIPT STOP` / `SCRIPT STATUS` | control the loaded script |
| `POWER SLEEP ON/OFF`, `POWER STATUS` | light-sleep policy and estimates |
| `LUA INIT/EXEC/DEINIT` | engine control |

Example output line:

```json
{"ts":12345,"addr":"AA:BB:CC:DD:EE:FF","type":"public","rssi":-45,"name":"Sensor_A","uuids":["180A"],"manu":{"id":"004C","data":"0102"},"tx_power":-8,"flags":"06"}
```

Example Lua hook (uploaded via `SCRIPT LOAD`):

```lua
function on_adv(addr, addr_type, rssi, name, uuids, manu_id, manu_data)
  return rssi > -70            -- false/0 suppresses the advertisement
end
function transform(addr, json)
  return json                  -- may rewrite the outgoing JSON line
end
```

## Documentation index

| Need | Where |
|------|-------|
| Product spec & design | `harness/00-global-context/project_overview.md` (historical plan: `docs/archive/qwen_featuer.md`) |
| Coding rules / error codes | `harness/00-global-context/coding_rules.md` |
| Per-feature specs + acceptance criteria | `harness/01-features/` |
| Current project status | `status/LATEST.md` |
| Bug history & deferred items | `bug_check/README.md` |
