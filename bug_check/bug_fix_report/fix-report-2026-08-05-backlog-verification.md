# Bug Fix Report — Stage 0–3 Backlog Verification (2026-08-05)

## Source

Tracker: `bug_check/stage-all-eval-2026-08-04.md` (listed "剩余 28 个" / 28 remaining)
Verified against: firmware v1.0.0, HEAD `d94e413` (= origin/master)

## Outcome: zero genuine remainders — no code changes needed

Independent claim-by-claim verification of all 28 "remaining" items against
current source found **every item already closed**. The tracker was stale:
it was committed in `082bf34`, and the follow-up fix commit `1eb7d04`
("fix: resolve 23 bugs from 2026-08-04 all-stage evaluation") closed the
backlog the same day — but the tracker document was never updated.

| Disposition | Count | Items |
|-------------|:-----:|-------|
| Fixed (commit `1eb7d04`, per `fix-report-2026-08-04.md`) | 23 | B-S2-3, M1, M3, M-S2-1, M-S2-2/L1, M-S2-4, M-S3-3, M-S3-4, M-S3-5, M-S3-6, M-S3-7, M-S3-8, M-S3-9, L4, L5, L-S2-1, L-S2-2, L-S2-3, L-S2-4, L-S3-1, L-S3-4, L-S3-5, L-S3-6 |
| Deferred by design (documented, accepted) | 2 | M-S2-3 (scan queue reused + `xQueueReset`), M-S2-5 (pipeline infinite-loop task) |
| Rejected | 3 | L3 (console is USB-Serial/JTAG via IDF VFS; no TinyUSB init needed — eval's own note agrees), M-S2-2 duplicate of L1, L-S3-6 diagnosis (bug real, characterization wrong — fixed as data loss) |

## Verification evidence (claim → current code)

| ID | Evidence in v1.0.0 source |
|----|---------------------------|
| B-S2-3 | `ble_scan.c`: `portMUX_TYPE s_dedup_mux` guards `s_dedup_check` and the start-time `memset` — explicit "B-S2-3 fix" comments |
| M1 | `usb_cdc_console.c`: timeout discards partial data, returns -503 — "M1 fix" comment |
| M3 | `littlefs_storage.c`: `storage_file_exists` takes `s_mutex` |
| M-S2-1 | `ble_nimble_init.c`: `ble_svc_gap_device_name_set()` rc checked + logged |
| M-S2-2 / L1 | `ble_nimble_init.c`: `ble_deinit` stops active scan, calls `nvs_flash_deinit()` — "M-S2-2 fix" comments |
| M-S2-4 | `ble_scan.c`: atomic `s_queue_drop_count` + `ble_scan_get_drop_count()`; STATUS JSON `queue_drops` |
| M-S2-3 | design: queue created once, `xQueueReset` on start/stop (see `ble_scan.c`) |
| M-S2-5 | design: pipeline task is an intentional `for(;;)` loop (`scan_pipeline.c`) |
| L3 | `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y` — peripheral owned by IDF startup; hardware suites prove the console works |
| L4 | `ble_scan.c`: `ble_scan_stop` drains queue via `xQueueReset` — "L4 fix" |
| L5 | `filter_if.h`: `filter_rule_t` no longer has an `active` field |
| L-S2-1 | `ble_nimble_init.c`: deinit waits 200 ms after `nimble_port_stop()` — "B-S2-1, L-S2-1 fix" |
| L-S2-2 | `ble_scan.c`: interval/window clamped to 0x0004..0x4000 — "L-S2-2 fix" |
| L-S2-3 | `ble_scan.c`: `ble_scan_set_params` returns -411 while scanning — "L-S2-3 fix" |
| L-S2-4 | `scan_pipeline.c`: `pipeline_stop` prints `uxTaskGetStackHighWaterMark2()` — "L-S2-4 fix" |
| M-S3-3 | `script_mgmt.c`: `script_stop` clears `on_adv`/`transform` globals — "M-S3-3 fix" |
| M-S3-4 | `lua_port.c`: hook errors classified -614 / -610 / -613 — "M-S3-4 fix" |
| M-S3-5 | `script_mgmt.c`: timeout compared in tick domain — "M-S3-5 fix" |
| M-S3-6 | `lua_port.c`: `LUA_HOOK_INSTR_INTERVAL = 1000` — "M-S3-6 fix" |
| M-S3-7 | `cli_commands.c`: RSSI via `strtol` + -128..127 range check (carried into Stage 4 CLI) |
| M-S3-8 | `cli_commands.c`: SCRIPT CHUNK even-length + strict-hex + max-size validation |
| M-S3-9 | `scan_pipeline.c` / `lua_port.c`: `on_adv` passes all 7 spec args — "M-S3-9 fix" |
| L-S3-1 | `lua_port.c`: `string.dump` nil'd out of the string library — "L-S3-1 fix" |
| L-S3-4 | `lua_port.c`: single `lua_settop(0)` resets — "L-S3-4 fix" |
| L-S3-5 | `script_mgmt.c`: upload buffer doubles as script cache — "L-S3-5 fix" |
| L-S3-6 | `script_mgmt.c`: chunk limit reserves terminator byte — "L-S3-6 fix" |

## Fresh verification run (2026-08-05, no code changed since v1.0.0)

- Host tests: **67/67 pass** (`tests/host/build/test_runner.exe`, exit 0)
- `test_bridge_hw.py` on COM12: **32 passed, 0 failed**
- `test_power_hw.py` on COM12: **14 passed, 0 failed**

## Actions taken this pass (documentation only)

- Tracker `stage-all-eval-2026-08-04.md` updated to final status (was stale)
- `bug_check/README.md` consolidated into a closed-bug index
- `status/LATEST.md` refreshed (still pointed at the 2026-07-21 eval state)
- `.gitignore` extended for session log files

## Lessons learned

- Fix rounds must update their tracker documents in the same commit —
  this backlog looked open for a day purely because the doc lagged the code.
- Verifying claims against source (not the tracker) remains mandatory:
  every one of the 28 "open" items was already closed.
