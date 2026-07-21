# Coding Rules

## 1. Error Codes

Common errors shared across all modules:

| Code | Name | Meaning |
|------|------|---------|
| 0 | OK | Success |
| -1 | ERR_NULL_PTR | NULL pointer argument |
| -2 | ERR_INVALID_ARG | Invalid argument value |
| -3 | ERR_NO_MEM | Static buffer exhausted |
| -4 | ERR_BUSY | Resource busy |
| -5 | ERR_TIMEOUT | Operation timed out |
| -6 | ERR_NOT_INIT | Module not initialized |
| -7 | ERR_ALREADY_INIT | Module already initialized |
| -8 | ERR_NOT_FOUND | Item not found |
| -9 | ERR_FULL | Queue or buffer full |
| -10 | ERR_EMPTY | Queue or buffer empty |
| -11 | ERR_OVERFLOW | Value or buffer overflow |
| -12 | ERR_NOT_SUPPORTED | Feature not supported in this build |

Module-specific ranges:

| Module | Range | Prefix |
|--------|-------|--------|
| ADV parser | -100 to -199 | ADV_ERR_* |
| JSON encoder | -200 to -299 | JSON_ERR_* |
| Filter engine | -300 to -399 | FILTER_ERR_* |
| BLE scan | -400 to -499 | BLE_ERR_* |
| USB console | -500 to -599 | USB_ERR_* |
| Lua engine | -600 to -699 | LUA_ERR_* |
| LittleFS | -700 to -799 | FS_ERR_* |
| Pipeline | -800 to -899 | PIPE_ERR_* |
| CLI | -900 to -999 | CLI_ERR_* |

Each module defines its specific codes within its range. Example:
```c
#define ADV_ERR_TRUNCATED_DATA   (-101)
#define ADV_ERR_UNKNOWN_TYPE     (-102)
#define JSON_ERR_BUFFER_FULL     (-201)
#define JSON_ERR_ESCAPE_FAIL     (-202)
```

## 2. Memory Rules

- **Application code**: prefer static allocation. Avoid direct malloc/free in business logic.
- **Libraries (cJSON, MicroPython, LittleFS, esp_console)**: heap allocation is allowed and expected.
- **NULL check** every pointer at every module boundary. Return `ERR_NULL_PTR` on failure.
- **Length check** every buffer write. Never write past the declared size.
- Use `sizeof(buf)` for stack/static buffers; pass explicit size for function parameters.
- Strings are always null-terminated; reserve the last byte for `\0`.

## 3. Buffer Sizes

| Buffer | Size | Location |
|--------|------|----------|
| USB RX | 256 B | `usb_if.h` |
| USB TX | 512 B | `usb_if.h` |
| BLE ADV raw | 62 B | `proto_if.h` (max AD data per spec) |
| Device name | 32 B | `proto_if.h` |
| Filter rules max | 16 | `filter_if.h` |
| JSON line max | 512 B | `json_if.h` |

## 4. Naming Conventions

| Element | Pattern | Example |
|---------|---------|---------|
| Static (file-scope) function | `s_<module>_<action>` | `s_filter_match_mac` |
| Public function | `<module>_<action>` | `filter_add_rule` |
| Struct | `<module>_<name>_t` | `filter_rule_t` |
| Enum | `<module>_<name>_e` | `filter_type_e` |
| Enum value | `<MODULE>_<NAME>` | `FILTER_TYPE_MAC` |
| Macro / constant | `MODULE_<NAME>` | `FILTER_MAX_RULES` |
| Error code | `<MODULE>_ERR_<NAME>` | `BLE_ERR_SCAN_TIMEOUT` |

Module names (lowercase): `adv_parse`, `json_enc`, `filter`, `ble_scan`, `usb_con`, `lua_eng`, `littlefs`, `pipeline`, `cli`.

## 5. Layering

```
Layer 4: cli  (user-facing commands)
Layer 3: pipeline  (orchestration)
Layer 2: filter | json_enc | littlefs
Layer 1: adv_parse | ble_scan | usb_con
Layer 0: proto_if  (shared types, no logic)
```

- Upper layers depend on lower layers only.
- No reverse dependencies. A lower layer must never call or include an upper layer.
- Cross-module data flows through `proto_if.h` types only.

## 6. Platform

- ESP-IDF v5.x
- NimBLE stack (no Bluedroid)
- C11 standard (`-std=gnu11`)
- No C++ in application code
- No WiFi in v1 (disable WiFi component in sdkconfig)
- FreeRTOS (SMP disabled for v1; pin tasks to core 0)

## 7. Filter Logic

- **AND** between different filter types (MAC, name, RSSI, UUID, manufacturer ID, AD type).
- **OR** within the same filter type (multiple MAC rules: match any).
- Maximum 16 rules total across all types.
- Empty filter set = pass all.
- Filter evaluation is short-circuit: first failing type stops evaluation.

## 8. Concurrency

Three FreeRTOS tasks:

| Task | Priority | Core | Role |
|------|----------|------|------|
| BLE host | 20 (highest) | 0 | NimBLE callbacks, copy raw ADV, enqueue |
| Pipeline | 10 | 0 | Dequeue, parse, filter, encode, output |
| USB CLI | 5 (lowest) | 0 | Read USB RX, parse commands, send responses |

Rules:
- BLE scan callback must **copy only** — no processing, no logging, no blocking.
- Queue depth: 32 entries. If full, increment drop counter and return.
- Mutex for shared resources (filter rule table, LittleFS).
- Pipeline task owns the JSON encoder output buffer.

## 9. Fault Isolation

- Every function return code is checked by the caller.
- Const pointers used for read-only inter-module data.
- Input validation at every module boundary (NULL, size, range).
- Task watchdog timeout: 5 seconds. Each task must feed the watchdog in its main loop.
- On queue full: increment drop counter, do not block the BLE task.

## 10. Inter-Module Data

All modules share `proto_adv_report_t` defined in `interfaces/proto_if.h` (parsed AD fields: addr, name, UUIDs, manufacturer data, RSSI, etc.). Reports are passed as **read-only const pointers** between modules. Buffers are **caller-owned**.

## 11. Byte Order

- UUIDs: big-endian hex strings, e.g., `"180A"` (UUID16), `"0000180A-0000-1000-8000-00805F9B34FB"` (UUID128).
- Manufacturer ID: big-endian hex, e.g., `"004C"` (Apple).
- MAC address: big-endian hex with colons, e.g., `"AA:BB:CC:DD:EE:FF"`.

## 12. Logging

- Use ESP-IDF logging macros: `ESP_LOGE`, `ESP_LOGW`, `ESP_LOGI`, `ESP_LOGD`.
- Each module defines its own tag: `static const char *TAG = "ADV_PARSE";`
- Default log level: `INFO`. Configurable via CLI command.
- Log output goes to USB CDC serial (same channel as JSON output, but JSON is machine-parseable while logs are human-readable).
- Logs and JSON share the USB TX path; pipeline serializes access.
