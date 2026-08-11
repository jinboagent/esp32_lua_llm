# Fix Report — H1/H2/H3 from the 2026-08-11 evaluation

**Date:** 2026-08-11 22:30
**Source:** [`stage-all-eval-2026-08-11.md`](../stage-all-eval-2026-08-11.md)
**Firmware:** v1.0.0 + H1/H2/H3 fixes (uncommitted at time of writing)

## Summary

| ID | Finding | Resolution | Verified |
|----|---------|-----------|----------|
| H1 🟠 | Lua error/result text spliced into JSON unescaped → invalid JSON on `LUA EXEC` errors/results, `SCRIPT END` compile errors, filter echoes | `json_escape_str()` helper + escaped emit in all dynamic-text paths | host 5 unit tests + 3 CLI/bridge regression tests; live `-612` response now parses |
| H2 🟡 | Overlong line (-504) left its tail in the RX stream, re-parsed as a command | drain-to-EOL in `usb_console_read_line` (consecutive-EOF budget + 8 KB cap; Ctrl+C preserved via pushback) | live 300-char line → `Read error: -504` and nothing else |
| H3 🟠 | `script_if.h` recommended 512-byte `SCRIPT CHUNK`s — impossible over the 255-char USB line (max 121 B) | documented the 121-byte transport cap in `script_if.h` + README (buffer bump deferred to v2 candidates) | doc review |
| H4 ⚪ | Lua pool fragmentation risk (watch item) | **not changed** — needs soak test + STATUS observability first | — |

Bonus finding while fixing: the **host suite had not been buildable since the
N1/N2 fixes** (2026-08-10) — `cli_commands.c` gained `esp_reset_reason()`
without a host shim. Added `tests/host/esp_system.h`; host suite rebuilt and
extended (67 → 76 tests).

## Changes

### H1 — JSON escaping
- `interfaces/json_if.h`: `json_escape_str()` contract (escapes `"`, `\`,
  control chars; truncation never splits an escape sequence → output always
  valid JSON).
- `firmware/components/json_enc/json_encoder.c`: implementation, shared by
  ESP32 and host builds.
- `firmware/components/cli/cli_commands.c`: escaped `LUA EXEC` result/msg,
  `FILTER ADD` value echo, `FILTER LIST` pattern echo, unknown-type echo.
- `firmware/components/bridge/lua_llm_bridge.c`: escaped `script_end`
  compile-error msg (+ `json_enc` added to the component REQUIRES).

Live before/after for `LUA EXEC return +++`:

```text
before: {"status":"error","cmd":"lua_exec","code":-612,"msg":"[string "return +++"]:1: ..."}
after:  {"status":"error","cmd":"lua_exec","code":-612,"msg":"[string \"return +++\"]:1: ..."}
```

### H2 — overflow drain
`firmware/components/usb/usb_cdc_console.c`: on buffer overflow the reader
now drains to end-of-line before returning -504. First attempt bounded
TOTAL loop iterations and still leaked 4 tail bytes; final version counts
only consecutive EOFs (40 × 5 ms ≈ 200 ms silence budget) with an 8 KB
absolute cap, and pushes back a Ctrl+C arriving mid-drain so interrupts
are never swallowed.

### H3 — chunk-size documentation
- `interfaces/script_if.h`: transport limit documented (121-byte payload
  per `SCRIPT CHUNK` over USB CDC).
- `README.md`: command table note + fixed the stale example JSON (nested
  `manu`, `type` as string) and FILTER type casing.

### Tests added
- `tests/host/test_json_encoder.c`: TC-15..19 for `json_escape_str`
  (basics, control chars, Lua-error shape, truncation validity, NULL args).
- `tests/host/test_cli.c`: escaped EXEC result / EXEC error / filter value
  echo.
- `tests/host/test_bridge.c`: escaped `script_end` compile-error msg.
- `tests/host/test_stubs.{h,c}`: `stub_lua_exec_ret/result`,
  `stub_upload_end_err` controls.
- `tests/host/esp_system.h`: reset-reason shim (restores host buildability).
- `test_ble_lua_hw.py` L9 check now asserts the drain leaves no re-parsed
  tail (was the H2 tracker); L3 syntax check asserts the response is valid
  JSON (was the H1 tracker).

## Verification (COM12, 2026-08-11 21:5x–22:2x)

| Suite | Result |
|-------|--------|
| host (`test_runner.exe`, -Werror clean) | **76/76** |
| `test_ble_lua_hw.py` | **45/45** |
| `test_ble_peer_hw.py` | **11/11** (capture threshold tuned to WinRT adv interval: ≥2 exact-field lines / 8 s) |
| `test_bridge_hw.py` | **32/32** |
| `test_power_hw.py` | **14/14** |

Build: ESP-IDF build clean, app 603 KB (62 % of partition free), flashed
and hash-verified on COM12.

## Open items carried forward
- **H4**: pool-fragmentation soak test + `free_heap`/Lua-pool metrics in
  STATUS (needs a small feature addition before the soak can measure).
- v2 candidate list now also includes raising `USB_RX_BUFFER_SIZE` (kills
  the 121-byte chunk cap) and fixing the host-only JSON encoder's 2-digit
  `\u` escapes (test-path only; firmware uses cJSON and is correct).
