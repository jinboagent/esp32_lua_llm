# v1.0.0 All-Stage Re-Evaluation — 2026-08-05

> ✅ **ALL FINDINGS CLOSED (2026-08-10)** — B1/B2/B3/B4/B6/B7 fixed, B5
> accepted + documented; verified 67/67 host tests, bridge 32/32, power
> 14/14 on COM12. See
> `bug_fix_report/fix-report-2026-08-10-eval-2026-08-05.md`.
> Document preserved as the historical audit record.

> Full project audit after v1.0.0 release. All 13 features across 4 stages implemented.
> Previously reported 66 bugs: 61 fixed, 2 deferred-by-design, 3 rejected. 0 critical remain.

---

## Bugs Found (7 total, all low/medium)

### 🟠 Medium (2)

#### B1. Data race on `s_script_running` / `s_script_loaded`

**Files**: `script_mgmt.c:21-22`, `scan_pipeline.c:78`, `cli_commands.c:89,128-129,139,530,546,559-560`

Both flags are plain `static bool` with no `volatile`, `_Atomic`, or mutex protection. Writers (CLI task) and readers (Pipeline task on core 0, or CLI's own `cli_get_state`) race under C11 memory model.

```c
// script_mgmt.c
static bool s_script_loaded = false;
static bool s_script_running = false;

// scan_pipeline.c:78 — read from Pipeline task
if (script_is_running() && lua_engine_has_func("on_adv") == 1)
```

On dual-core ESP32-S3 this is a genuine data race. Practical impact is low — Lua mutex inside `lua_engine_call_on_adv` mitigates — but the behavior is technically undefined.

**Fix**: Change to `atomic_bool` (pattern already used for `s_running` and `s_scanning`).

---

#### B2. `esp_pm_lock_create` failure silently degrades

**File**: `power_mgmt.c:71-79, 87-89`

When `CONFIG_PM_ENABLE=y` but lock creation fails, `power_init` prints a printf and returns 0. `power_hold_activity` becomes a no-op:

```c
if (s_activity_lock == NULL) { return; }
```

Result: device can enter light sleep during BLE scanning → console TX FIFO underrun → dropped host output. The only user-visible signal is the boot-time printf, easily missed.

**Fix**: Either return error from `power_init` when lock creation fails, or call `power_enable_sleep(false)` as fallback.

---

### 🟡 Low (5)

| ID | File(s) | Description |
|----|---------|-------------|
| B3 | `main.c` → `lua_llm_bridge.c:95` | `bridge_init()` never called in main's init chain. Works by coincidence (static zero-init = BRIDGE_STATE_IDLE). Maintenance risk. |
| B4 | `lua_port.c:133-134 vs 139` | Pool allocator OOM guard uses unaligned `nsize` but tracking uses aligned size → accounting drift over many alloc/free cycles. Hard limit (`s_heap_top`) is correct, only the soft limit degrades. |
| B5 | `scan_pipeline.c:203`, `cli_commands.c:111-112` | `pipeline_get_stats` copies `s_stats` struct while pipeline task concurrently increments fields → transiently inconsistent snapshot. Cosmetic. |
| B6 | `lua_port.c:297-301` | `vSemaphoreDelete(s_lua_mutex)` called after `lua_engine_unlock()` — another task could acquire the mutex between unlock and delete. Requires concurrent deinit to trigger. |
| B7 | `ble_scan.c:72` | Dedup table uses `last_seen_us == 0` as "empty slot" marker. `esp_timer_get_time()` starts at 0 at boot, so first ~microseconds could coincide. Not practically exploitable. |

---

## Concurrency Safety

### Correct patterns

| Mechanism | Where |
|-----------|-------|
| `atomic_bool` | `s_running` (pipeline), `s_scanning` (ble_scan) |
| `atomic_uint` | `s_queue_drop_count` (ble_scan) |
| `portMUX_TYPE` + spinlock | `s_dedup` table (ble_scan) |
| FreeRTOS mutex | `s_lua_mutex` — Lua VM + filter engine |
| FreeRTOS queue | `s_scan_queue` — inherently thread-safe |

### Remaining gaps

| Variable | Writers | Readers | Risk |
|----------|---------|---------|:----:|
| `s_script_running/loaded` | CLI task | Pipeline + CLI tasks | 🟠 (B1) |
| `s_stats` | Pipeline task | CLI task | 🟡 (B5) |

### Mutex coupling note

`lua_engine_lock/unlock` protects BOTH the Lua VM AND the filter engine rule table. Filter modifications from CLI use the Lua mutex; pipeline filter evaluation holds it. Works because CLI is single-threaded and the pipeline already needs the mutex for Lua hooks in the same iteration. But if `lua_engine_deinit` destroys the mutex while pipeline is running, the filter evaluation would take a destroyed semaphore. Acceptable for v1.0.0 but fragile.

---

## Buffer Overflow Audit

**All clear — no buffer overflows found.**

| Operation | Mechanism | Safe? |
|-----------|-----------|:-----:|
| CLI_EMIT macro | `snprintf` + truncation check → -903 | ✅ |
| s_append (bridge) | `vsnprintf` with remaining calculation | ✅ |
| sscanf filter values | `%15s` → `char[32]`, `%31s` → `char[32]` | ✅ |
| Hex chunk decode | `chunk_len < sizeof(chunk_buf)=512` | ✅ |
| Bridge sandbox scan | `memcmp` bounded by `p + tok_len <= end` | ✅ |
| script_upload_chunk | `total_received + len > SCRIPT_MAX_SIZE - 1` | ✅ |
| BLE raw data copy | `min(disc->length_data, BLE_ADV_DATA_MAX_LEN)` | ✅ |
| Lua pool allocator | `s_heap_top + needed > LUA_MEMORY_LIMIT` | ✅ |

---

## Spec Compliance

### F4.1 (CLI Commands) — 7/7 ACs passed
### F4.2 (LLM Bridge) — 9/9 ACs passed
### F4.3 (Power Management) — v1.0.0-scoped ACs passed; USB suspend + PMIC measurement deferred to v2 (documented in feature doc)

1 minor spec deviation: `FILTER ADD` response includes extra `"type"`/`"value"` fields beyond spec's `"index"`. Forward-compatible.

---

## Test Coverage

| Module | Host Tests | HW Tests | Coverage |
|--------|:---------:|:--------:|:--------:|
| AD Parser (proto) | 14 | — | ~70% |
| JSON Encoder | 14 | — | ~75% |
| Filter Engine | 14 | — | ~65% |
| CLI Commands | 15 | 22 checks | ~80% |
| LLM Bridge | 10 | — | ~80% |
| Power Mgmt | via CLI tests | 10 checks | ~60% |
| BLE Scan/Init | — | 1 (test_ble.py) | ~10% |
| Lua Engine | — | 6 (test_lua.py) | ~15% |
| Script Mgmt | — | 5 (test_script.py) | ~20% |
| Storage | — | — | 0% |
| USB | — | — | 0% |

**Total**: 66 host C tests + ~60 HW checkpoints across 9 Python scripts.

### Coverage gaps

- **No CI pipeline** — no `.github/workflows/`, all tests manual
- **Storage + USB** have 0 direct tests
- **Python tests fragile** — hardcoded `COM12`, `time.sleep()` instead of polling, no test framework
- **Host tests share one binary** — one crash loses all suite results
- **No mocking framework** — stubs are hand-written; pipeline integration unittests not feasible
- **test_results.txt stale** — shows only 42 tests (parser+JSON+filter), CLI/bridge results not recorded

---

## Resource Lifecycle

| Module | init | deinit | Notes |
|--------|:----:|:------:|-------|
| CLI | ✅ | N/A | Static, no teardown needed |
| Bridge | ❌ | N/A | `bridge_init()` never called (B3) |
| Power | ✅ | N/A | Static |
| Lua | ✅ | ✅ | Via `LUA DEINIT` CLI command |
| Pipeline | ✅ | N/A | Infinite loop task, never exits |
| BLE | ✅ | Not called | `ble_deinit` exists but unused |
| Storage | ✅ | N/A | Static |
| USB | ✅ | N/A | Static |

No shutdown path — acceptable for always-on embedded device.

---

## Summary

| Metric | Count |
|--------|:-----:|
| Bugs (medium) | 2 |
| Bugs (low) | 5 |
| Bugs (critical) | **0** |
| Spec deviations | 1 minor (harmless extra JSON fields) |
| Buffer overflows | **0** |
| Concurrency gaps | 2 (B1 + B5) |
| Host tests | **66** (all pass) |
| HW test checkpoints | **~60** |

**Overall: v1.0.0 is production-ready.** The 7 remaining items are all low-severity. The most impactful is B4 (silent lock failure → dropped console output). Fix B1 (atomic script flags) before enabling SMP or core-pinning changes.
