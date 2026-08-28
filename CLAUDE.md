# ESP32-S3 BLE Sniffer Dongle

> Context-optimized project overview. Full specs in `harness/`. Root `README.md` has build/usage details.

## Architecture

ESP32-S3 passive BLE advertisement scanner → AD parse → C filters → optional Lua hooks → JSON lines over USB CDC → host PC LLM analyzes → generates Lua scripts → deploys back via `SCRIPT LOAD`. 13 features across 4 stages.

## Current Status: v1.0.0 — ALL STAGES COMPLETE

- All 13 features implemented and hardware-verified; all bug backlogs closed (0 open items)
- Verification: host tests 67/67 · `test_bridge_hw.py` 32/32 · `test_power_hw.py` 14/14 (COM12)
- Components: `usb`, `storage`, `ble` (NimBLE scan + dedup + pipeline), `proto`, `json_enc`, `filter`, `lua` (5.4 + sandbox + script mgmt), `cli` (state machine), `bridge` (text-line upload), `power` (light sleep)
- Next candidate work: host-side LLM-loop tooling (scan JSON → LLM → Lua → upload); see `status/LATEST.md`

## Key Rules

- **Reuse before building**: Check ESP-IDF built-ins and ESP Component Registry before writing custom code
- **malloc/free**: Allowed for libraries; application logic uses static buffers (Lua runs on a static pool allocator)
- **Error codes**: module-specific ranges in `interfaces/*_if.h` (CLI -9xx, bridge -6xx/-8xx, storage -7xx, BLE -4xx, pipeline -8xx)
- **Lua sandbox**: whitelist libs only (base/string/table/math/utf8); uploads scanned for forbidden tokens (os./io./debug./require/...) → -612
- **Concurrency**: `lua_State` protected by `lua_engine_lock/unlock`; dedup table by spinlock; scan state atomic

## Build

```
# Host tests
cmake --build tests/host/build && tests/host/build/test_runner.exe

# ESP32
scripts/build.bat && scripts/flash.bat        (or idf.py build / flash / monitor)
python tests/hw/test_bridge_hw.py      # F4.1+F4.2 hardware suite
python tests/hw/test_power_hw.py       # F4.3 hardware suite
```

## Key Files

| Path | Purpose |
|------|---------|
| `interfaces/*_if.h` | Public APIs — the only cross-module surface |
| `firmware/components/` | Implementation modules |
| `main/main.c` | Init chain + USB command loop |
| `tests/host/` | Unity host tests (67) |
| `harness/00-global-context/` | Product spec, coding rules, build env, git workflow |
| `harness/01-features/` | All 13 implemented feature specs |
| `bug_check/README.md` | Consolidated bug tracking (closed) |
| `status/LATEST.md` | Latest status snapshot |
