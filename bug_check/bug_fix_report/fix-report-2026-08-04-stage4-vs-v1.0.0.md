# Bug Fix Report — Stage 4 Evaluation vs v1.0.0 (2026-08-04)

## Source
Bug report: `bug_check/stage4-eval-2026-07-21.md`
Evaluated against: firmware v1.0.0 (commits `3dacab2`, `0b57ad9`)

**Context:** the evaluation was written 2026-07-21, before Stage 4 was
implemented. This pass verifies every claim against the shipped v1.0.0
code and fixes what genuinely remained.

## Summary Table

| Severity | Total | Already fixed by Stage 4 | Fixed now | Disagree/adjusted | Deferred |
|----------|:-----:|:------------------------:|:---------:|:-----------------:|:--------:|
| Critical | 2 (+2 module gaps) | 4 | 0 | 0 | 0 |
| Medium   | 7 (+4 bridge gaps)  | 11 | 0 | 0 | 0 |
| Low      | 5 (+1 bridge gap)   | 4 | 2 | 0 | 1 (v2, pre-documented) |

## Already Fixed by the Stage 4 Implementation (verified in v1.0.0 code)

| ID | Claim | Verified fix | Where |
|----|-------|--------------|-------|
| B-S4-1 | `process_command` no NULL check | `cli_process_command` returns `-902` on NULL cmd/response/len | `cli_commands.c` dispatch entry |
| B-S4-2 | CLI+pipeline unlocked filter access | `lua_engine_lock()` around FILTER ADD/CLEAR (B-S3-6 fix carried into cli component); pipeline evaluates under the same lock | `cli_commands.c`, `scan_pipeline.c` |
| M-S4-1 | No state machine | IDLE/SCANNING/SCRIPT_RUNNING machine, `-911` guards on all restricted transitions | `cli_commands.c` (`cli_get_state`, `s_state_error`) |
| M-S4-2 | SCAN INTERVAL missing | `SCAN INTERVAL <ms>` with 10..10000 validation → `ble_scan_set_params` | `cli_commands.c` h_scan |
| M-S4-3 | RSSI atoi no range check | `strtol` + `-128..127` validation (M-S3-7 fix) | `cli_commands.c` h_filter |
| M-S4-4 | SCRIPT CHUNK hex unvalidated | even-length + hex-char + max-size validated decode (M-S3-8 fix) | `cli_commands.c` h_script |
| M-S4-5 | FILTER LIST returns only count | `"filters":[{type,pattern|threshold},...]` array | `cli_commands.c` h_filter LIST |
| M-S4-6 | STATUS lacks free_storage | `storage_get_free_space()` → `"free_storage"` field | `cli_commands.c` h_status |
| M-S4-7 | SCRIPT LOAD vs BEGIN naming | Both exist by design: `SCRIPT LOAD` = spec text-line protocol (F4.2 bridge), `SCRIPT BEGIN/CHUNK` = retained hex extension; documented in feature docs | `cli_commands.c`, `lua_llm_bridge.c` |
| L-S4-1 | SCAN START no rollback | Rollback on pipeline-start failure (B-S3-7 fix) | `cli_commands.c` h_scan |
| L-S4-4 | `goto filter_done` fragile flow | Eliminated by the cli rewrite (direct returns) | `cli_commands.c` |
| L-S4-5 | VERSION 0.3.0 ≠ spec 1.0.0 | `CLI_FW_VERSION "1.0.0"` | `cli_commands.c` |
| F4.2 gaps | bridge_if.h / lua_llm_bridge.c / text-line protocol / sandbox scan AC#7 / upload interruption AC#4 / 30 s timeout | All implemented: `bridge` component, per-line sandbox scan (`-612`), abort-on-command, `SCRIPT_UPLOAD_TIMEOUT_MS 30000` | commit `3dacab2` |
| F4.3 gap | power module 0% | Implemented (reduced scope): automatic light sleep when idle, wake-on-USB-command, PM activity lock while scanning, `POWER` CLI commands, current estimates | commit `0b57ad9` |
| §四 busy-loop claim | pipeline `vTaskDelay(50)` never yields for PM | Blocking delay + tickless idle permits light sleep whenever pipeline stopped (F4.3 design) | `scan_pipeline.c`, sdkconfig |

## Fixed Now (this pass)

### L-S4-2 — SCAN STOP ignored subsystem results
- **Root cause**: `pipeline_stop()` / `ble_scan_stop()` return codes discarded; a failed stop reported `ok`.
- **Fix**: check both return codes; on failure emit `{"status":"error","cmd":"scan_stop","msg":"stop failed: <r1>/<r2>"}`. PM activity lock is still released unconditionally.
- **Verification**: host stubs return 0 → 67/67 host tests pass; hardware SCAN STOP flows in both hardware suites.

### L-S4-3 — duplicated response output
- **Report claim adjusted**: the "prints to UART0 / wrong terminal" part is incorrect — the console is USB-Serial/JTAG (`CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`), not UART0. The real defect: `usb_console_send_json()` already emits the response (printf + fflush), and `main.c` printed it again with a `< ` prefix — every response appeared twice on the stream.
- **Fix**: removed the duplicate `printf("< %s\n")`. The `> <command>` echo is retained intentionally: it is a single-line command echo useful for interactive debugging, does not duplicate any response, and host tooling already filters non-JSON lines.
- **Verification**: host tests; hardware suites parse responses unambiguously.

## Deferred (pre-documented, unchanged)

- **USB suspend detection** (F4.3 spec / eval §四): USB-Serial/JTAG exposes no bus-suspend signal on this hardware → deferred to v2, documented in `feature_power_management.md` Implementation Notes.

## Not Applicable

- **`tests/harness/` on-target C harness** (eval §五): this project's established test strategy across all stages is host-side Unity tests (`tests/host`, now 67 tests incl. 15 CLI + 10 bridge) plus Python hardware suites driven over USB (`test_bridge_hw.py` 32 checks, `test_power_hw.py` 14 checks). The on-target harness directory was never adopted; treating its absence as a bug would contradict the stage-0..3 precedent.

## Test Results
- Host tests: **67/67 pass** (14 proto + 14 json + 14 filter + 15 cli + 10 bridge)
- ESP32 build: BUILD_OK (incremental on v1.0.0)
- Flash: COM12, Hash of data verified
- Hardware: `test_bridge_hw.py` 32/32, `test_power_hw.py` 14/14 (rerun after flash)
