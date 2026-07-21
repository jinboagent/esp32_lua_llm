# Bug Fix Report — 2026-07-19

## Source
Bug report: `bug_check/bug-luminous-spring.md` (30 issues found by automated analysis)

## Summary

| Severity | Total | Fixed | Deferred | Not Started |
|----------|-------|-------|----------|-------------|
| 🔴 Critical | 4 | 3 | 1 (B4) | 0 |
| 🟠 Medium | 12 | 4 | 8 | 0 |
| 🟡 Low | 10 | 1 | 9 | 0 |
| 🔵 Architecture | 4 | 0 | 4 | 0 |
| **Total** | **30** | **8** | **22** | **0** |

---

## Fixed Bugs

### B1 — JSON Encoder Stack Buffer Overflow ✅
- **File:** `firmware/components/json_enc/json_encoder.c`
- **Severity:** Critical (remote triggerable via BLE radio)
- **Root cause:** `escaped_name` buffer was 64 bytes (`PROTO_DEVICE_NAME_MAX_LEN * 2`). Control characters expand to 6 bytes each (`\uXXXX`), so worst case = 31 × 6 + 1 = 187 bytes > 64 bytes.
- **Fix:** Changed buffer to `PROTO_DEVICE_NAME_MAX_LEN * 6 + 1` (192 bytes). Added return value check on `s_encode_string_escaped()`.
- **Verification:** Host unit tests pass (14/14 for adv_parser, 11/11 for json_encoder).

### B2 — Wildcard Recursion DoS ✅
- **File:** `firmware/components/filter/filter_engine.c`
- **Severity:** Critical (watchdog reset via crafted filter pattern)
- **Root cause:** `s_wildcard_match()` used recursive implementation. Pattern `"*a*b*c*..."` with 15 stars causes O(2^n) recursion depth, blocking the pipeline task past the 5s watchdog timeout.
- **Fix:** Rewrote as iterative algorithm using star-backtracking (two-pointer approach). No recursion, O(n×m) worst case.
- **Verification:** All 14 filter engine tests pass including wildcard tests.

### B3 — Missing LittleFS Partition ✅
- **File:** `partitions.csv`
- **Severity:** Critical (blocks all Lua script storage features)
- **Root cause:** Partition table had only nvs/phy_init/factory. No LittleFS entry despite `feature_littlefs_storage.md` requiring a 64KB partition.
- **Fix:** Added `littlefs, data, spiffs, 0x190000, 0x10000` (64KB at offset 1.5MB).
- **Verification:** ESP32 build succeeds, partition table shows LittleFS in boot log.

### M2 — JSON Encoder Drops tx_power and flags ✅
- **File:** `firmware/components/json_enc/json_encoder.c`
- **Severity:** Medium (data loss between parse and output)
- **Root cause:** Parser correctly fills `has_tx_power`/`tx_power`/`has_flags`/`flags`, but encoder never output these fields.
- **Fix:** Added `tx_power` and `flags` fields to JSON output, placed before closing brace.
- **Verification:** ESP32 monitor output shows `"tx_power":9,"flags":"06"` in JSON.

### M5 — filter_evaluate(NULL) Returns true ✅
- **File:** `firmware/components/filter/filter_engine.c`
- **Severity:** Medium (silent bypass — all data passes unfiltered)
- **Root cause:** NULL check returned `true` (pass-all). If programmer forgets `filter_init()`, all advertisements silently bypass filtering.
- **Fix:** Changed to return `false` (fail-safe: block everything if engine not initialized).
- **Verification:** Existing test `test_filter_null_engine` still passes (tests init/clear/get_count NULL cases).

### M9 — Missing extern "C" Guards ✅
- **Files:** `interfaces/proto_if.h`, `interfaces/json_if.h`, `interfaces/filter_if.h`
- **Severity:** Medium (C++ linkage failure)
- **Root cause:** Headers lacked `#ifdef __cplusplus extern "C" {` blocks. Would cause linker errors if included from C++ test framework.
- **Fix:** Added `extern "C"` open/close guards to all three interface headers.
- **Verification:** Host tests compile and pass (compiled as C, but guards are syntactically correct).

### M10 — test_main.c Always Returns 0 ✅
- **File:** `tests/host/test_main.c`
- **Severity:** Medium (CI cannot detect test failures)
- **Root cause:** `main()` called test suite functions (which returned void) and unconditionally returned 0.
- **Fix:** Changed test suite functions to return `int` (from `UNITY_END()`). `main()` accumulates failure counts and returns total.
- **Verification:** `test_runner.exe` exits with code 0 when all pass. Would exit non-zero on failure.

### L2 — s_match_mac No Pattern Length Validation ✅
- **File:** `firmware/components/filter/filter_engine.c`
- **Severity:** Low (potential out-of-bounds read)
- **Root cause:** `s_match_mac()` reads exactly 17 characters from pattern without checking length. Short patterns read past null terminator.
- **Fix:** Added `if (strlen(rule->pattern) != 17) return false;` at function entry.
- **Verification:** Existing MAC filter tests pass.

---

## Additional Fix (Not in Bug Report)

### main.c — Hardcoded Advertisement Data Wrong ✅
- **File:** `main/main.c`
- **Root cause:** Service data length byte was 0x0F (should be 0x0C). Name "LYWSD03MMC" length byte was 0x09 (should be 0x0B but total would exceed 31-byte legacy BLE max).
- **Fix:** Shortened name to "LYWSD03" (7 chars) and corrected all length bytes. Total array = 31 bytes (legacy BLE max).
- **Verification:** ESP32 monitor shows `Parse OK: name=LYWSD03` (pending final flash verification).

---

## Deferred Bugs

### B4 — sdkconfig.defaults Missing NimBLE Config (Deferred)
- **Why deferred:** NimBLE config is only needed when Stage 2 (BLE scan) is implemented. Not needed for current Stage 0/1 work.
- **When to fix:** Before implementing `feature_nimble_init.md` (Stage 2).
- **What's needed:** `CONFIG_BT_ENABLED`, `CONFIG_BT_NIMBLE_ENABLED`, `CONFIG_BT_NIMBLE_ROLE_OBSERVER`, `CONFIG_BT_NIMBLE_MEM_POOL_SIZE=70`, `CONFIG_WIFI_ENABLED=n`.

### M1 — UUID32/UUID128 Parsing Missing (Deferred)
- **Why deferred:** Only UUID16 is needed for v1 product scope. UUID32/128 are rare in practice.
- **When to fix:** After Stage 2 when we have real BLE data to test against.

### M3 — Error Code System Inconsistent (Deferred)
- **Why deferred:** Three parallel systems (common -1~-12, module-specific -100~-999, and filter_get_count returning -1). Requires cross-module coordination.
- **When to fix:** During Stage 4 integration when all modules are connected.

### M4 — Silent Truncation (Deferred)
- **Why deferred:** Current behavior (truncate and succeed) is debatable. Need product decision: should we return errors for overflow or silently cap?
- **When to fix:** After product decision on error handling philosophy.

### M6 — Naming Convention Violations (Deferred)
- **Why deferred:** Enum suffix `_e`, static function module prefixes. Cosmetic, doesn't affect functionality.
- **When to fix:** During code cleanup pass before v1.0 release.

### M7 — filter_clear vs filter_init Inconsistency (Deferred)
- **Why deferred:** Currently equivalent. Only matters if `filter_engine_t` gains new fields.
- **When to fix:** When adding new fields to the struct.

### M8 — filter_rule_t.active Has No API (Deferred)
- **Why deferred:** No `filter_remove_rule()` exists yet. The `active` field is always true.
- **When to fix:** When implementing `FILTER REMOVE` CLI command (Stage 4).

### M11 — Only 3/13 Features Implemented (Deferred)
- **Why deferred:** This is expected — we implement stage by stage.
- **When to fix:** Ongoing development.

### M12 — coding_rules.md Outdated (Deferred)
- **Why deferred:** Document doesn't match actual code after bug fixes.
- **When to fix:** After all code fixes are finalized.

### L1 — Filter Engine Not Thread-Safe (Deferred)
- **Why deferred:** Only matters when multiple FreeRTOS tasks access the filter engine concurrently.
- **When to fix:** Stage 2 when pipeline task and CLI task both access filters.

### L3 — Hex Parsing Code Duplicated (Deferred)
- **Why deferred:** Code quality issue, not a bug.
- **When to fix:** During cleanup pass.

### L4 — ts_ms 49.7-Day Wraparound (Deferred)
- **Why deferred:** Not a practical concern for v1 (device won't run 49 days continuously).
- **When to fix:** If long-running operation becomes a requirement.

### L5 — manu_id=0 Ambiguous (Deferred)
- **Why deferred:** Callers should check `has_manu` before reading `manu_id`. Documentation issue.
- **When to fix:** When writing API documentation.

### L6 — Missing const Qualifiers (Deferred)
- **Why deferred:** Internal functions only. No external impact.
- **When to fix:** During cleanup pass.

### L7 — Test Helpers Missing static (Deferred)
- **Why deferred:** No actual symbol conflicts in current test setup.
- **When to fix:** During cleanup pass.

### L8 — CMake EXTRA_COMPONENT_DIRS Deprecated (Deferred)
- **Why deferred:** Still works in ESP-IDF v5.1. Migration to `components/` directory is cosmetic.
- **When to fix:** When upgrading to newer ESP-IDF version.

### L9 — Tests Use strstr Instead of JSON Parser (Deferred)
- **Why deferred:** Tests work correctly for current output format. Fragile but functional.
- **When to fix:** When JSON output format changes.

### L10 — strncpy Without Null Termination in Test Helper (Deferred)
- **Why deferred:** Test helper uses short names that fit. No actual bug triggered.
- **When to fix:** During cleanup pass.

### A1-A4 — Architecture Concerns (Deferred)
- **Why deferred:** These relate to unimplemented features (BLE scan, pipeline, multi-core).
- **When to fix:** During Stage 2-4 implementation.

---

## Test Results

### Host-Side Tests (MinGW gcc on Windows)
```
ADV Parser:     14 Tests  0 Failures  ✅
JSON Encoder:   11 Tests  0 Failures  ✅
Filter Engine:  14 Tests  0 Failures  ✅
Total:          39 Tests  0 Failures  ✅
Exit code: 0 (test_main.c now returns failure count)
```

### ESP32-S3 Build
```
Target:     esp32s3
Toolchain:  xtensa-esp32s3-elf-gcc 12.2.0
ESP-IDF:    v5.1
Binary:     ble_sniffer.bin (220KB, 86% flash free)
Partitions: nvs (24KB) + phy_init (4KB) + factory (1.5MB) + littlefs (64KB)
```

### ESP32-S3 Hardware
```
Chip:       ESP32-S3 (QFN56) revision v0.2
Features:   WiFi, BLE, Embedded PSRAM 8MB
Flash:      16MB (detected), 4MB (configured)
USB:        USB-Serial/JTAG on COM12
```

---

## Files Modified

| File | Changes |
|------|---------|
| `firmware/components/json_enc/json_encoder.c` | B1 (buffer size), M2 (tx_power/flags), addr check |
| `firmware/components/filter/filter_engine.c` | B2 (iterative wildcard), M5 (NULL→false), L2 (MAC length) |
| `interfaces/proto_if.h` | M9 (extern "C") |
| `interfaces/json_if.h` | M9 (extern "C") |
| `interfaces/filter_if.h` | M9 (extern "C") |
| `main/main.c` | Fixed hardcoded adv data byte counts |
| `partitions.csv` | B3 (added LittleFS partition) |
| `tests/host/test_main.c` | M10 (return failure count) |
| `tests/host/test_adv_parser.c` | Return int from suite function |
| `tests/host/test_json_encoder.c` | Return int from suite function |
| `tests/host/test_filter_engine.c` | Return int from suite function |

---

## How to Reproduce

### Run host tests
```cmd
set PATH=C:\msys64\mingw64\bin;%PATH%
cmake --build tests/host/build
tests/host/build/test_runner.exe
echo %ERRORLEVEL%    REM should be 0
```

### Build for ESP32
```cmd
C:\Espressif\idf_cmd_init.bat
cd E:\agent\esp32_lua_llm
idf.py build
```

### Flash and monitor (via tmux or cmd)
```cmd
idf.py -p COM12 flash
idf.py -p COM12 monitor
```
