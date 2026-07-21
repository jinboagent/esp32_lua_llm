# ESP32-S3 BLE Sniffer Dongle

> Context-optimized project overview. Full specs in `harness/`. Future-stage docs in `harness/02-future/` (not auto-loaded).

## Architecture

ESP32-S3 passive BLE advertisement scanner → JSON lines over USB CDC → host PC LLM analyzes → generates scripts → deploys back. 13 features across 4 stages.

## Current Status (Stage 0-1)

**Implemented:**
- Stage 0: USB CDC console (`firmware/components/usb/`), LittleFS storage (`firmware/components/storage/`)
- Stage 1: NimBLE-based AD parser (`firmware/components/proto/`), cJSON-based encoder (`firmware/components/json_enc/`), filter engine (`firmware/components/filter/`), BLE init/scan (`firmware/components/ble/`)
- Pipeline orchestration in `main/main.c`
- 39 host-side unit tests (`tests/host/`)

**Pending:** Stages 2-4 (see `harness/02-future/`)

## Key Rules

- **Reuse before building**: Check ESP-IDF built-ins (cJSON, NimBLE, esp_console, VFS, esp_pm) and ESP Component Registry before writing custom code
- **malloc/free**: Allowed for libraries; avoid in application logic
- **Error codes**: Global -1~-12; module-specific ranges in `interfaces/*_if.h`
- **#ifdef ESP_PLATFORM**: Dual-implementation pattern — NimBLE/cJSON on ESP32, hand-written fallback for host tests

## Build

```
# Host tests
cmake --build tests/host/build && tests/host/build/test_runner.exe

# ESP32 (via ESP-IDF cmd environment)
idf.py build && idf.py flash && idf.py monitor
```

## Key Files

| Path | Purpose |
|------|---------|
| `interfaces/*_if.h` | Public APIs (proto, json, filter, usb, storage, ble) |
| `firmware/components/` | Implementation modules |
| `tests/host/` | Unity-based host tests |
| `main/main.c` | Application entry + CLI |
| `harness/00-global-context/` | Coding rules, build env, git workflow |
| `harness/01-features/` | Implemented feature specs (Stage 0-1) |
| `harness/02-future/` | Future feature specs (Stage 2-4) — NOT auto-loaded |
| `bug_check/README.md` | Consolidated bug tracking |
| `status/LATEST.md` | Latest status snapshot |
