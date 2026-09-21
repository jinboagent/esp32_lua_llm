# Bug Fix Report — 2026-08-04

## Source

Bug report: `bug_check/stage-all-eval-2026-08-04.md` (28 remaining bugs, Stages 0-1/2/3).
Every reported bug was verified against the actual source before fixing.
Commit: see git log (fix commit follows this report).

## Summary Table

| Severity | Total reported | Fixed | Deferred | Rejected (false positive) |
|----------|:--------------:|:-----:|:--------:|:-------------------------:|
| 🔴 Critical | 1 | 1 | 0 | 0 |
| 🟠 Medium | 11 | 11 | 0 | 0 |
| 🟡 Low | 16 | 11 | 2 | 3 |
| **Total** | **28** | **23** | **2** | **3** |

Plus **5 additional bugs found during this review** (not in the eval) — all fixed (marked N1–N5 below).

## Fixed Bugs

### 🔴 Critical

| ID | File | Root cause | Fix | Verified by |
|----|------|-----------|-----|-------------|
| B-S2-3 | `ble_scan.c` | `memset(s_dedup)` in `ble_scan_start` (CLI task) races with `s_dedup_check` writes from GAP event handler (NimBLE host task) when events from the previous scan are still in flight | Dedup table protected by a `portMUX_TYPE` spinlock; both `s_dedup_check` and the start-time `memset` take it | Build; logic review (race window eliminated) |

### 🟠 Medium

| ID | File | Root cause | Fix |
|----|------|-----------|-----|
| M1 | `usb_cdc_console.c` | On timeout with partial data buffered, returned the partial line (positive count) instead of -503 — violates the header contract; caller would execute truncated commands | Timeout now discards partial data and returns -503 |
| M3 | `littlefs_storage.c` | `storage_file_exists` opened the file without the storage mutex, racing with write/delete | Takes `s_mutex` around the existence probe |
| M-S2-1 | `ble_nimble_init.c` | `ble_svc_gap_device_name_set()` return ignored | Return checked, failure logged |
| M-S2-2 / L1 | `ble_nimble_init.c` | `ble_deinit` didn't stop an active scan and didn't release NVS | Stops scan first; calls `nvs_flash_deinit()` (also on the sync-timeout path) |
| M-S2-4 | `ble_scan.c` | Queue-full drops were invisible | `s_queue_drop_count` (atomic) incremented on drop; exposed via `ble_scan_get_drop_count()`; `STATUS` JSON now includes `queue_drops` |
| M-S3-3 | `script_mgmt.c` | `script_stop` left `on_adv`/`transform` in the Lua global table | New `lua_engine_clear_func()`; `script_stop` clears both hook globals (per spec: "compiled script is released") |
| M-S3-4 | `lua_port.c` | Both hook callers returned -613 for every `lua_pcall` failure | Errors classified: timeout → -614, `LUA_ERRMEM` → -610, else -613 (same logic as `lua_engine_exec`) |
| M-S3-5 | `script_mgmt.c` | `(now - last_tick) * portTICK_PERIOD_MS` can overflow uint32_t | Comparison moved to tick domain: `diff > pdMS_TO_TICKS(timeout)` — no multiplication |
| M-S3-6 | `lua_port.c` | Debug hook installed with `count=100`; spec (`feature_lua_port.md` §101) requires 1000 | All 3 `lua_sethook` sites use `LUA_HOOK_INSTR_INTERVAL = 1000`; effective budget now ~100k instructions ≈ 10 ms as designed |
| M-S3-7 | `main.c` | `FILTER ADD RSSI` cast `atoi()` straight to int8_t (silent wrap for out-of-range) | `strtol` + full-consumption check + range check -128..127, error response on violation |
| M-S3-8 | `main.c` | `SCRIPT CHUNK` hex decode: non-hex chars silently became 0, odd length dropped last nibble, >512 bytes silently truncated | Validated decoder: even length, strict hex digits, max 512 bytes; returns error JSON otherwise |

### 🟡 Low

| ID | File | Fix |
|----|------|-----|
| L4 | `ble_scan.c` | `ble_scan_stop` drains the queue (`xQueueReset`) after `ble_gap_disc_cancel` |
| L5 | `filter_if.h`, `filter_engine.c` | Dead `active` field removed (was always true, never cleared) |
| L-S2-1 | `ble_nimble_init.c` | Normal `ble_deinit` now waits 200 ms after `nimble_port_stop()` for the host task to exit (same sequence as the B-S2-1 timeout path) |
| L-S2-2 | `ble_scan.c` | Converted scan interval/window clamped to BLE spec range 0x0004..0x4000 before `ble_gap_disc` |
| L-S2-3 | `ble_scan.c` | `ble_scan_set_params` returns -411 while scanning (params take effect at next start) |
| L-S2-4 | `scan_pipeline.c` | `pipeline_stop` prints `uxTaskGetStackHighWaterMark2()` so stack headroom is observable |
| L-S3-1 | `lua_port.c` | `string.dump` nil'd out of the whitelisted string library (bytecode exposure) |
| L-S3-4 | `lua_port.c` | Redundant `lua_pop` + `lua_settop(0)` pairs replaced with a single `lua_settop(0)` |
| L-S3-5 | `script_mgmt.c` | `script_run` reuses the upload buffer as a cache of the saved script; cache invalidated in `script_upload_begin` |
| L-S3-6 | `script_mgmt.c` | **Eval misdiagnosed this as "unreachable else (harmless)"** — the else WAS reachable when `total_received == SCRIPT_MAX_SIZE` and silently truncated the last script byte. Fixed by reserving 1 byte for the NUL terminator in `script_upload_chunk` (max payload now 8191 bytes), which also fixes N3 |
| M-S3-9 | `scan_pipeline.c`, `lua_port.c`, `lua_if.h` | `on_adv` hook now passes all 7 spec parameters: `(addr, addr_type, rssi, name, uuids, manu_id, manu_data)` — was only `(addr, rssi, name)`. `uuids` is a Lua table of "%04X" strings, `manu_data` a hex string, absent fields are nil |

### Additional bugs found during this review (N1–N5)

| ID | File | Description | Fix |
|----|------|-------------|-----|
| N1 | `usb_cdc_console.c` | `timeout_ms==0` with partial data looped forever (busy-loop) | Unified EOF path: elapsed >= timeout → -503 |
| N2 | `littlefs_storage.c` | If `storage_init` failed, all other functions crashed on `xSemaphoreTake(NULL)` | Every function returns -701 when not mounted |
| N3 | `script_mgmt.c` | `script_run` wrote `buffer[read_len]` which is OOB when the file is exactly `SCRIPT_MAX_SIZE` | Read limited to `SCRIPT_MAX_SIZE - 1` (pairs with L-S3-6) |
| N4 | `scan_pipeline.c` | `s_running` plain bool written by CLI task, read by pipeline task (same class as fixed B-S2-2) | `atomic_bool` |
| N5 | `lua_port.c` | `lua_engine_call_transform` never initialized `json_out`; if the hook returned nothing, callers checking `json_out[0]` read uninitialized stack | Output buffer zeroed on entry |

## Deferred Bugs

| ID | Why deferred | When to fix |
|----|--------------|-------------|
| M-S2-3 | Scan queue is intentionally created once and reused (`xQueueReset` on start/stop); deleting it would require synchronization with the pipeline task's consumer for no functional gain | If scan module gains a real deinit |
| M-S2-5 | Pipeline task is an intentional infinite loop (this was the B1 fix — deleting tasks caused dead code/crashes); a `pipeline_deinit` would reintroduce that complexity | Not needed with current lifecycle (init once at boot) |

## Rejected (disagree with eval)

| ID | Reason |
|----|--------|
| L3 | Not a bug: TinyUSB/USB-Serial-JTAG console is initialized by ESP-IDF startup code before `app_main` (`CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`, `CONFIG_TINYUSB_CDC_ENABLED=y` in sdkconfig.defaults). The eval's own note "(fcntl is sufficient)" agrees. |
| M-S2-2 (as separate from L1) | Same bug as L1 — one fix covers both entries |
| L-S3-6 diagnosis | Agree there is a bug, disagree with characterization: the else branch was reachable and harmful (see L-S3-6 above) |

## Known divergences noted but NOT changed (out of eval scope)

1. **`transform` hook signature** — spec says `transform(addr, parsed_data_table)`; implementation passes `transform(addr, json_string)`. Changing it would break existing scripts; the JSON-string form is arguably friendlier for LLM-generated scripts. Needs a spec-vs-code decision.
2. **`lua_engine_deinit` vs concurrent lock waiters** — a task calling `lua_engine_lock()` between the unlock and `vSemaphoreDelete` in deinit could race. Realistic mitigation is "don't deinit while scanning"; a full fix needs a refcount on the engine.

## Test Results

- **Host tests:** 42/42 pass (14 parser + 14 JSON encoder + 14 filter engine), `-Wall -Wextra -Werror` clean
- **ESP32 build (IDF v5.1, esp32s3):** success, `ble_sniffer.bin` generated, no warnings in changed files
- **On-target flash/monitor:** NOT performed — device configured on COM12 but not connected at fix time. Verify by: `.\flash.bat` then `.\monitor.bat`; exercise `SCAN START`, `SCRIPT BEGIN/CHUNK/END/RUN/STOP`, `FILTER ADD RSSI -90`, `STATUS` (check `queue_drops`)

## Lessons learned

- The eval's line-number citations drifted from actual code (e.g. M1 cited lines 73-75; actual logic was a few lines off) — always verify against source.
- Two eval entries (L1, M-S2-2) were the same bug listed under different stages.
- One "harmless unreachable" item (L-S3-6) was actually reachable data loss — severity labels need verification too.
