# Fix Report — H4 Observability + Soak Setup (2026-08-11)

**Source:** [`stage-all-eval-2026-08-11.md`](../stage-all-eval-2026-08-11.md) (H4 watch item)
**Firmware:** v1.0.0 + H1/H2/H3 (`a6c1b3b`) + H4 metrics (this change)

## What H4 was

Watch item, not a confirmed bug: the Lua static pool allocator has no
coalescing/splitting, so a long-running `transform` hook that allocates per
advertisement could fragment the 128 KB pool. There was **no runtime
observability** — peak was only printed at `LUA DEINIT` — so a soak test
could not measure anything.

## Change: make the soak measurable

| File | Change |
|------|--------|
| `interfaces/lua_if.h` | `void lua_engine_pool_stats(uint32_t *used, uint32_t *peak)` (NULL-tolerant) |
| `firmware/components/lua/lua_port.c` | implementation over existing `s_pool_used` / `s_peak_used` accounting |
| `firmware/components/cli/cli_commands.c` | STATUS gains `"free_heap"` (`esp_get_free_heap_size()`) and `"lua_pool":{"used":N,"peak":N}` |
| `tests/host/esp_system.h` | shim gains `esp_get_free_heap_size()` (host build was broken without it once STATUS used it) |
| `tests/host/test_stubs.c` | stub returns used=1111/peak=2222 |
| `tests/host/test_cli.c` | `test_status_fields` asserts the new fields |
| `README.md` | STATUS row lists the new metrics |
| `soak_test.py` (new) | 2 h soak: SCAN START + fragmenting `transform` (variable-size `string.rep` churn), STATUS every 5 min, adv-flow window each sample, pass/fail summary |

STATUS stays within the 512-byte TX buffer (≈ +70 bytes).

## Soak design (pass criteria, printed by soak_test.py)

- `reset_reason` unchanged for the whole run (no silent resets)
- `lua_pool.used` stable: max−min ≤ 2 KB after warmup (fragmentation would show as monotonic growth)
- `free_heap` stable: max−min ≤ 8 KB
- adv lines observed in every 5 s sample window (no stream stall)
- `queue_drops` not growing after warmup

## Verification (before starting the soak)

- Host: **76/76** (14 adv + 19 json + 14 filter + 18 cli + 11 bridge)
- `test_bridge_hw.py` **32/32** · `test_power_hw.py` **14/14** ·
  `test_ble_lua_hw.py` **45/45** (strict JSON parser accepts the extended STATUS)
- Flashed and hash-verified on COM12

## Soak status

Launched 2026-08-11 (2 h, `soak_test.py 2`). Verdict to be appended here
and reported in `status/LATEST.md` when the run completes.
