# BLE Sniffer Dongle Project Code Evaluation Report

## Context

ESP32-S3 BLE Sniffer Dongle — a USB dongle that passively scans BLE advertisements, parses, filters, and outputs JSON. The v1 plan schedules 13 features across 4 stages; currently only the 3 Stage 1 pure-computation modules (AD parser, JSON encoder, filter engine) + 1 static demo main.c are implemented. The evaluation covers all existing source code, interface headers, test code, build configuration, and documentation.

---

## 🔴 Critical Bugs

### B1. JSON encoder — stack buffer overflow (`firmware/components/json_enc/json_encoder.c:86`)

```c
char escaped_name[PROTO_DEVICE_NAME_MAX_LEN * 2];  // = 64 bytes
s_encode_string_escaped(escaped_name, sizeof(escaped_name), report->name);
```

- `s_encode_string_escaped` emits `\uXXXX` (6 bytes) for control characters `< 0x20`
- BLE device names are at most 31 characters; worst case: 31 × 6 + 1 = **187 bytes** > the 64-byte buffer
- A malicious/corrupted BLE device can trigger the stack overflow simply by embedding control characters in the AD name
- **This is a vulnerability remotely triggerable by external BLE radio signals**
- Fix: `escaped_name` should be sized `PROTO_DEVICE_NAME_MAX_LEN * 6` (192 bytes)

### B2. Filter engine — recursive wildcard DoS (`firmware/components/filter/filter_engine.c:60-86`)

- `s_wildcard_match` uses a recursive implementation, with O(2^n) exponential complexity on `*a*a*a*...*` patterns
- An attacker injects via CLI a filter rule containing 15 `*`s (max 31 chars), paired with a 31-character device name
- This can block the pipeline task past the 5-second watchdog timeout, causing a system reset
- Fix: switch to a non-recursive implementation for the embedded setting

### B3. Partition table missing the LittleFS partition (`partitions.csv`)

- The partition table has only nvs/phy_init/factory, **no LittleFS/storage partition at all**
- This directly blocks: Lua script storage (F0.2), Lua engine (F3.1), script management (F3.2), the CLI SCRIPT command (F4.1), the LLM bridge (F4.2)
- `feature_littlefs_storage.md` defines a 64KB LittleFS partition, but the partition table has no corresponding entry

### B4. sdkconfig.defaults missing key configuration

| Missing item | Impact |
|--------|------|
| NimBLE (`CONFIG_BT_ENABLED`, `CONFIG_BT_NIMBLE_ENABLED`, `CONFIG_BT_NIMBLE_ROLE_OBSERVER`, `CONFIG_BT_NIMBLE_MEM_POOL_SIZE=70`) | BLE cannot initialize |
| LittleFS (`CONFIG_LITTLEFS_ENABLED`) | Script storage unavailable |
| `CONFIG_WIFI_ENABLED=n` | WiFi enabled by default, wasting ~20KB RAM |
| `CONFIG_FREERTOS_SMP=n` | Dual-core SMP not disabled as the spec requires |
| CPU frequency = 160MHz (spec requires 240MHz) | Reduced throughput |

---

## 🟠 Medium Severity

### M1. UUID32/UUID128 parsing missing (`proto_adv_parse.c`)

- The `project_overview.md` v1 Scope explicitly includes "UUID16/32/128"
- `proto_adv_parse.c` handles only UUID16 (AD types 0x02-0x03)
- The `#define` constants for AD types 0x04-0x07 are defined, but the switch statement does not handle them — they fall into `default: break`
- This is unfinished functionality

### M2. JSON encoder silently drops the `flags` and `tx_power` fields

- The parser correctly fills in `has_flags`/`flags`/`has_tx_power`/`tx_power`
- The encoder never emits these two fields to JSON
- Neither a documented limitation nor an intended omission — this is a data-loss path

### M3. Three parallel, mutually incompatible error-code schemes

| Source | NULL pointer error code | Scheme |
|------|---------------------|------|
| `coding_rules.md` §1 | `-1` (`ERR_NULL_PTR`) | global generic codes |
| `proto_if.h` | `-102` | module range (-100~-199) |
| `filter_if.h` | `-302` | module range (-300~-399) |

- `filter_get_count` returns `-1` (outside the -300~-399 range)
- The generic error-code table in `coding_rules.md` is **entirely unused** in the actual code

### M4. Parser/filter silently truncate, invisible to callers

- `s_parse_uuid16_list`: UUIDs beyond 10 are dropped, returns 0 (success)
- `s_parse_manufacturer_data`: manufacturer data beyond 31 bytes is dropped, returns 0 (success)
- `filter_add_rule`: patterns ≥ 32 characters are truncated to 31, returns 0 (success)
- Callers have no way to judge data integrity

### M5. `filter_evaluate(NULL, ...)` returns `true` (allows through)

- If the programmer forgets `filter_init` → all data **silently bypasses filtering**, with no crash and no error
- The other filter functions return error codes for NULL; only `filter_evaluate` is inconsistent
- It should return `false` (block) or use an assert

### M6. Naming conventions violated in multiple places

- **Enum types** should end with `_e`: `proto_addr_type_t` → should be `proto_addr_type_e`; `filter_type_t` → should be `filter_type_e`
- **Static functions** should carry a module prefix: `s_parse_name` → should be `s_proto_parse_name`; `s_match_name` → should be `s_filter_match_name`; `s_encode_mac` → should be `s_json_encode_mac`
- `main.c` uses `printf` instead of the `ESP_LOG*` macros required by the conventions

### M7. `filter_clear` inconsistent with `filter_init`

- `filter_init` does `memset(eng, 0, sizeof(filter_engine_t))` (clears everything)
- `filter_clear` clears only `rules[]` and `rule_count` (partial clear)
- Currently equivalent, but if `filter_engine_t` gains a new field later (e.g. `bool enabled`), `filter_clear` will miss it

### M8. `filter_rule_t.active` field has no public API

- The struct has an `active` field, and `s_evaluate_type` checks it
- But there is no `filter_remove_rule()` or `filter_set_active()` API
- The field is always `true` (set right when added) — **dead interface surface**

### M9. Interface headers lack `extern "C"` guards

- `proto_if.h`, `filter_if.h`, and `json_if.h` all lack `#ifdef __cplusplus / extern "C" {` blocks
- The project claims to be "host-testable"; if a C++ test framework is used later, linking will fail

### M10. `test_main.c` always returns 0

- `main()` unconditionally does `return 0` after executing `UNITY_END()`, discarding Unity's failure count
- CI cannot detect test failures via the exit code

### M11. Only 3/13 features have implementation code

- Implemented: proto_adv_parse, filter_engine, json_encoder (all pure-computation modules)
- The following features have **no source files at all**: BLE scan, NimBLE init, pipeline, Lua engine, script management, CLI commands, LLM bridge, power management, USB CDC console, LittleFS storage

### M12. `coding_rules.md` documentation outdated in multiple places

| Outdated content | Actual state |
|----------|----------|
| `proto_adv_report_t` definition (with `adv_data[62]`) | the actual struct has parsed fields |
| filter types include "manufacturer ID, AD type" | only NAME/UUID/RSSI/MAC implemented |
| BLE ADV raw buffer 64 bytes | `PROTO_ADV_DATA_MAX_LEN` = 31 |
| task priorities Pipeline=10, CLI=5 | `feature_scan_pipeline.md` says Pipeline=2, USB=1 |

---

## 🟡 Low Severity

### L1. Filter engine not thread-safe

- `filter_add_rule`/`filter_evaluate`/`filter_clear` read and write shared state, with no locks and no atomics
- Three FreeRTOS tasks (BLE callback/pipeline/CLI) may access it concurrently

### L2. `s_match_mac` does not validate the total pattern length

- It reads exactly 17 characters, does not check for extra characters, and does not check whether the pattern ends with `\0`
- `"AA:BB:CC:DD:EE:FF:extra"` would be accepted as a valid MAC

### L3. Duplicate hex parsing code

- Identical hex-digit parsing logic copy-pasted in `s_match_uuid` and `s_match_mac`
- It should be extracted into a shared `hex_digit()` helper function

### L4. `ts_ms` uses `uint32_t` — wraps around after ~49.7 days

- Milliseconds since device boot; wraps back to 0 after 49.7 days
- Long-running operation may see timestamp ordering become garbled

### L5. Ambiguous `manu_id=0` semantics

- When `has_manu=false`, `manu_id` defaults to 0
- 0x0000 is a valid assigned Company ID (Ericsson)
- A caller who forgets to check `has_manu` will read data that looks valid

### L6. Missing `const` qualifiers

- The `data` parameters of `s_parse_uuid16_list(data, ...)` and `s_parse_manufacturer_data(data, ...)` are not modified and should be `const uint8_t *`

### L7. Test helper functions missing `static`

- `s_make_report` (test_filter_engine.c) and `s_make_basic_report` (test_json_encoder.c) are not declared `static`, risking link symbol conflicts

### L8. CMake uses deprecated `EXTRA_COMPONENT_DIRS`

- ESP-IDF v5.x recommends using the project-root `components/` directory

### L9. Test assertions rely on `strstr` instead of structured verification

- `strstr(buf, "\"ts\":1000")` misjudges when the name field contains that substring
- A JSON parser or exact string comparison should be used instead

### L10. `strncpy` in `s_make_report` lacks explicit null termination

- `strncpy(r.name, name, PROTO_DEVICE_NAME_MAX_LEN - 1)` does not write a `\0` when name ≥ 31 characters
- The current tests use short names, but the helper itself is fragile

---

## 🔵 Architecture/Design Concerns

### A1. BLE scan core logic entirely unimplemented

- `main.c` is just a static demo running on hardcoded data
- NimBLE scan callbacks, FreeRTOS queue communication, and pipeline task scheduling have not been started at all
- What currently works is only the pure-computation modules that can be tested standalone on the host

### A2. All tasks pinned to Core 0

- The ESP32-S3 is dual-core; Core 1 sits idle
- BLE Host (priority 20) can preempt the pipeline (priority 10) for long stretches

### A3. Missing `_Static_assert` compile-time checks

- Struct changes cannot be automatically checked for ABI drift

### A4. JSON output and logs share the USB CDC channel with no serialization

- The spec requires pipeline serialization, but `main.c` uses `printf` to bypass the pipeline
- The host-side JSON Lines parser fails when it encounters a non-JSON log line

---

## 📊 Summary

| Severity | Count | Key items |
|--------|------|--------|
| 🔴 Critical | 4 | JSON stack overflow, wildcard DoS, missing LittleFS partition, missing sdkconfig |
| 🟠 Medium | 12 | missing UUID32/128, dropped flags/tx_power, messy error codes, silent truncation, NULL allow-through, naming violations, `filter_clear` inconsistency, dead `active` field, missing `extern "C"`, test_main exit code, implementation coverage 3/13, outdated docs |
| 🟡 Low | 10 | thread safety, MAC validation, duplicate hex code, ts_ms wraparound, ambiguous manu_id, missing const, missing static, CMake legacy, strstr tests, strncpy termination |
| 🔵 Architecture | 4 | core logic unimplemented, single-core scheduling, no `_Static_assert`, mixed logs/JSON |

---

## 📋 Recommended Fix Priorities

1. **Immediate**: the `json_encoder.c` escaped_name buffer overflow, make `s_wildcard_match` non-recursive
2. **As soon as possible**: add the `partitions.csv` LittleFS partition, and the NimBLE/LittleFS/WiFi/SMP settings in `sdkconfig.defaults`
3. **This stage**: unify the error-code scheme, complete naming-convention compliance, fix the `test_main.c` exit code, fix the outdated documentation
4. **Next stage**: implement UUID32/128 parsing, add the `flags`/`tx_power` JSON output, add thread-safety protection
5. **Ongoing**: add test coverage (control-character escaping, recursion depth, truncation boundaries, NULL filter_evaluate, etc.), change `strstr` assertions to structured verification
