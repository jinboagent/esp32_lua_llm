# All-Stage Bug Status — 2026-08-04

> ✅ **All closed (verified 2026-08-05)** — the 28 "remaining" items below were all
> handled in commit `1eb7d04` (23 fixed / 2 deferred-by-design / 3 rejected); verification evidence in
> `bug_fix_report/fix-report-2026-08-05-backlog-verification.md`.
> This document is kept as a historical record.

> Since the last evaluation, 23 bugs were fixed across 3 fix commits. 28 currently remain.

---

## Fixed (23, commit: ee690ae, d79ff1f, 69ae6d9)

| ID | Stage | Description | Commit |
|----|:---:|------|--------|
| B1 | 0-1 | Pipeline vTaskDelete dead code → changed to a `for(;;)` infinite loop | ee690ae |
| B2 | 0-1 | Filter not wired into the pipeline → added `pipeline_set_filter()` | ee690ae |
| B3 | 0-1 | NVS erase abort() crash → manual error check | ee690ae |
| B4 | 0-1 | USB getchar() blocking → `fcntl(O_NONBLOCK)` | ee690ae |
| M2 | 0-1 | ftell -1 → uint32_t overflow → `file_size < 0` check | d79ff1f |
| M4 | 0-1 | CLI missing FILTER ADD → fully implemented | ee690ae |
| M5 | 0-1 | BLE queue not cleared → `xQueueReset()` | ee690ae |
| L2 | 0-1 | fread return value unverified → `read != file_size` check | d79ff1f |
| L6 | 0-1 | \u00xx lowercase hex → `%02X` uppercase | d79ff1f |
| B-S2-1 | 2 | BLE sync timeout double-deinit race → `vTaskDelay(200)` | 69ae6d9 |
| B-S2-2 | 2 | `s_scanning` two-task race → `atomic_bool` | 69ae6d9 |
| B-S2-4 | 2 | weak XOR hash fake dedup → FNV-1a + linear probing + LRU | 69ae6d9 |
| B-S3-1 | 3 | malloc/free instead of a static pool → bump+free-list pool allocator | 69ae6d9 |
| B-S3-2 | 3 | sandbox blacklist bypass → whitelist: only base/string/table/math/utf8 enabled | 69ae6d9 |
| B-S3-3 | 3 | double execution (upload+run) → `lua_engine_compile_check()` trial compile | 69ae6d9 |
| B-S3-4 | 3 | lua_State accessed from two tasks without a lock → `lua_engine_lock/unlock()` | 69ae6d9 |
| B-S3-5 | 3 | Hook func_name NULL check missing → checks added to both functions | 69ae6d9 |
| B-S3-6 | 3 | CLI+filter concurrency race → filter operations wrapped in `lua_engine_lock` | 69ae6d9 |
| B-S3-7 | 3 | SCAN START no rollback → `ble_scan_stop()` called when the pipeline fails | 69ae6d9 |
| M-S3-1 | 3 | used-=osize underflow → pool allocator no longer has this problem | 69ae6d9 |
| M-S3-2 | 3 | peak memory label wrong → `s_peak_used` tracks it correctly | 69ae6d9 |
| L-S3-2 | 3 | package.path not cleared → package not loaded in whitelist mode | 69ae6d9 |
| L-S3-3 | 3 | used=0 masks leaks → pool allocator tracks via s_pool_used | 69ae6d9 |

---

## Remaining Bugs (28) — all now closed, see the note at the top of the document

### Stage 0-1 (6)

| ID | Severity | File | Description |
|----|:------:|------|------|
| M1 | 🟠 | `usb_cdc_console.c:73-75` | USB timeout returns partial data (positive) instead of -503 — violates the API contract |
| M3 | 🟠 | `littlefs_storage.c:137-146` | `storage_file_exists` does not hold the lock — races with write/delete |
| L1 | 🟡 | `ble_nimble_init.c:113-128` | `ble_deinit` does not stop scanning first and does not call `nvs_flash_deinit` |
| L3 | 🟡 | `usb_cdc_console.c:12-28` | `usb_console_init` does not initialize the TinyUSB hardware (fcntl is sufficient) |
| L4 | 🟡 | `ble_scan.c:163-178` | `ble_scan_stop` does not drain/reset the queue |
| L5 | 🟡 | `filter_if.h:47` | `filter_rule_t.active` is a dead field — always true |

### Stage 2 — BLE (10)

| ID | Severity | File | Description |
|----|:------:|------|------|
| B-S2-3 | 🔴 | `ble_scan.c:141` | Dedup memset races with concurrent writes from the GAP callback |
| M-S2-1 | 🟠 | `ble_nimble_init.c:81` | `ble_svc_gap_device_name_set()` return value not checked |
| M-S2-2 | 🟠 | `ble_nimble_init.c:113-128` | `ble_deinit` does not stop scanning first and does not call `nvs_flash_deinit` |
| M-S2-3 | 🟡 | `ble_scan.c:132` | Queue never destroyed (design decision, acceptable) |
| M-S2-4 | 🟡 | `ble_scan.c:106` | queue-full drops have no counter — not observable |
| M-S2-5 | 🟡 | `scan_pipeline.c` | no `pipeline_deinit()` (infinite-loop design) |
| L-S2-1 | 🟡 | `ble_nimble_init.c:119-120` | normal deinit does not wait for the host task to exit |
| L-S2-2 | 🟡 | `ble_scan.c:144-146` | scan parameter → NimBLE uint16_t unit conversion not validated |
| L-S2-3 | 🟡 | `ble_scan.c:185-199` | `ble_scan_set_params` allows changing parameters while scanning |
| L-S2-4 | 🟡 | `scan_pipeline.c:13` | 4096-byte stack + ~1200 bytes of local variables, no high-water mark check |

### Stage 3 — Lua (12)

| ID | Severity | File | Description |
|----|:------:|------|------|
| M-S3-3 | 🟠 | `script_mgmt.c:150` | `script_stop` does not clear functions from the Lua global table |
| M-S3-4 | 🟠 | `lua_port.c:463-468` | Hook call errors always return -613 — no distinction between timeout/OOM |
| M-S3-5 | 🟠 | `script_mgmt.c:52` | Tick→ms multiplication may overflow |
| M-S3-7 | 🟠 | `main.c:135` | RSSI `atoi` has no range validation (-128~+127) |
| M-S3-8 | 🟠 | `main.c:234-237` | SCRIPT CHUNK hex decoding has no validation |
| M-S3-6 | 🟡 | `lua_port.c:336` | Hook fires every 100 instructions (spec says 1000) |
| M-S3-9 | 🟡 | `scan_pipeline.c:78-79` | `on_adv` is passed only 3 arguments (spec requires 7) |
| L-S3-1 | 🟡 | `lua_port.c:195-217` | `string.dump` is still available in the whitelisted string library |
| L-S3-4 | 🟡 | `lua_port.c:465,472` | redundant `lua_pop` followed by `lua_settop` |
| L-S3-5 | 🟡 | `script_mgmt.c:124` | `script_run` re-reads from LittleFS every time |
| L-S3-6 | 🟡 | `script_mgmt.c:87-91` | unreachable else branch (harmless) |

---

## Summary

| Stage | 🔴 Critical | 🟠 Medium | 🟡 Low | Remaining | Fixed |
|------|:------:|:------:|:-----:|:----:|:------:|
| Stage 0-1 | 0 | 2 | 4 | **6** | 9 |
| Stage 2 | 1 | 2 | 7 | **10** | 3 |
| Stage 3 | 0 | 5 | 7 | **12** | 9 |
| Stage 4 | 4 | 7 | 5 | **16** | 0 |
| **Total** | **5** | **16** | **23** | **44** | **23** |

> Note: the Stage 4 LLM Bridge (0%) and Power Management (0%) remain entirely unimplemented. The CLI State Machine is still missing.
