# ESP32-S3 BLE Sniffer Dongle

A USB dongle that passively scans BLE advertisements, parses and filters them
(with optional on-device Lua scripting), and streams structured JSON lines to
a host PC over USB CDC. A host-side LLM can analyze the captured traffic and
generate Lua scripts that are deployed back to the device — an AI-driven BLE
analysis loop.

## Status: v1.0.0 released ✅

- **All 13 features across 4 stages implemented and hardware-verified** (see `status/LATEST.md`)
- **All evaluation findings closed** through 2026-08-11: H1 (JSON escaping) /
  H2 (overlong-line drain) / H3 (chunk-limit doc) fixed; H4 (Lua pool
  fragmentation) instrumented (`free_heap` + `lua_pool` in STATUS) with a
  2 h soak running (see `bug_check/README.md`)
- Verification on COM12 hardware: host tests **76/76** · `test_bridge_hw.py`
  **32/32** · `test_power_hw.py` **14/14** · `test_ble_lua_hw.py` **45/45** ·
  `test_ble_peer_hw.py` **11/11**

## Architecture

```
                            HOST PC
 +---------------------------------------------------------------------+
 |  collector / LLM tooling                test suites                  |
 |  - reads JSON adv lines                 - pyserial CLI drivers       |
 |  - analyzes, generates Lua              - bleak/WinRT controlled     |
 |  - SCRIPT LOAD upload                     BLE peer (PC advertises)   |
 +-------------------------- USB CDC (USB-Serial/JTAG) ----------------+
                    |   in: text commands (CR or LF; Ctrl+C = interrupt)
                    |   out: JSON lines (adv stream + command responses)
                    v
                            ESP32-S3 DONGLE
   NimBLE observer — passive, continuous discovery (BLE_HS_FOREVER)
        |  adv report: addr / addr_type / rssi / AD data / ts_ms
        v
   dedup (FNV-1a hash, ~1 s window, spinlock)  -->  raw ADV queue
        v
   pipeline task:
     AD parse (proto) -> C filter engine -> [Lua on_adv hook: keep/drop]
        v
     JSON encode (all dynamic text escaped) -> [Lua transform hook]
        v
     USB console TX (mutex-serialized lines)
```

Product loop (the reason the device exists): scan JSON → host LLM analyzes
→ LLM writes a Lua filter/transform → `SCRIPT LOAD` deploys it (sandboxed)
→ device streams only what matters. v1 is scan-only by design; BLE
connections/GATT are v2.

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
- **Continuous scan**: discovery runs with `BLE_HS_FOREVER` (no windowed
  restarts); `Ctrl+C` (0x03, no Enter) stops scan/script/upload instantly
- **Console contract**: CR, LF, and CRLF all terminate a line (PuTTY and
  scripts both work); overlong lines drain to end-of-line instead of
  leaking a re-parsed tail; every dynamic string in responses is JSON-escaped
- **Lua sandbox**: whitelist only (base/string/table/math/utf8, `string.dump` removed);
  uploads additionally scanned for `os.`/`io.`/`debug.`/`require`/etc. → reject `-612`
- **Concurrency**: `lua_State` mutex, dedup table spinlock, atomic scan state — all race findings from evaluations are fixed and regression-covered
- **Observability**: STATUS reports `reset_reason` (11 = USB reset on port
  close), `free_heap`, `lua_pool{used,peak}`, queue drops and pipeline counters
- **Power**: tickless light sleep when idle, wake-on-USB-command; a NO_LIGHT_SLEEP
  PM lock is held while scanning (console TX drops bytes if the SoC sleeps mid-stream)
- **Known hardware boundary (N3)**: closing the host COM port resets the
  chip (`ESP_RST_USB`) — host tools keep the port open (normal collector mode)

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
putty_sim_test.py      interactive-session simulation (CR endings, Ctrl+C)
cr_lf_test.py          line-terminator contract (CR / LF / CRLF)
capture_25s.py         25 s continuous-scan window check
soak_test.py           2 h H4 soak: scan + fragmenting transform + STATUS sampling
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

## Test strategy

Four layers; everything hardware-facing runs against the real dongle on
COM12 (one program owns the port at a time).

| Layer | Tool | Covers | Checks |
|-------|------|--------|:------:|
| Host unit | Unity + MinGW, `tests/host` (stubs for ble/lua/storage/power) | AD parser, JSON encoder + escaping, filter logic, CLI state machine, bridge protocol | 76 |
| HW command plane | `test_bridge_hw.py`, `test_power_hw.py` | CLI/bridge/state guards/sandbox on device, PM behavior | 32 + 14 |
| HW data plane | `test_ble_lua_hw.py` (ambient RF), `test_ble_peer_hw.py` (PC advertises via WinRT as a controlled peer) | JSON schema / ts monotonicity / dedup invariants, 7-arg hook ABI, suppression + transform on the live stream, v1 non-connectability | 45 + 11 |
| Interactive & soak | `putty_sim_test.py`, `cr_lf_test.py`, `capture_25s.py`, `soak_test.py` | terminal contract (CR/LF/Ctrl+C), continuous-scan windows, 2 h pool-fragmentation soak with `free_heap`/`lua_pool` sampling | — |

Principles (each learned from a real miss):

- **Keep the port open** for any in-scan check — closing COM12 resets the chip (N3).
- **Strict `json.loads` assertions** on every response; substring matching let H1 slip through.
- **Ambient RF is nondeterministic** — exact-field verification needs the controlled PC peer.
- **FreeRTOS/NimBLE behavior isn't host-stubbable** — the data plane is hardware-in-the-loop only.
- **Suites only see the first seconds after SCAN START** — long-window and soak tools exist to cover windowing/fragmentation bugs.

## Usage quick reference

Connect to the USB-Serial/JTAG console (COM12 @ 115200). Commands:

| Command | Effect |
|---------|--------|
| `STATUS` | state, scanning, queue_drops, filter_count, lua_ready, script_loaded/running, free_storage, free_heap, lua_pool used/peak, pipeline stats |
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
| `Ctrl+C` | interrupt: abort upload, stop script and scan immediately (no Enter needed) |

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
