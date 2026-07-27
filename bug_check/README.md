# Bug Tracking — BLE Sniffer Dongle

> Consolidated index. Original detailed reports archived in `archive/`.

## Summary

| Severity | Total | Fixed | Deferred |
|----------|-------|-------|----------|
| 🔴 Critical | 4 | 3 | 1 (B4: sdkconfig NimBLE) |
| 🟠 Medium | 12 | 4 | 8 |
| 🟡 Low | 10 | 1 | 9 |
| 🔵 Architecture | 4 | 0 | 4 |
| **Total** | **30** | **8** | **22** |

## Fixed (9 issues)

| ID | Description | Commit |
|----|-------------|--------|
| B1 | JSON encoder stack buffer overflow | d69f65e |
| B2 | Wildcard recursive DoS | d69f65e |
| B3 | LittleFS partition table missing | d69f65e |
| M2 | flags/tx_power not copied from NimBLE fields | d69f65e |
| M5 | NULL filter_evaluate crash | d69f65e |
| M9 | Missing extern "C" in headers | d69f65e |
| M10 | test_main exit code wrong | d69f65e |
| L2 | MAC address length check | d69f65e |
| L7 | Static helpers should be in .c | d69f65e |

## New Bugs Found (2026-07-19, post-refactor)

6 new issues found after Stage 0 implementation. See `archive/new_bugs_2026-07-19_00-00-58.md` for details.

## Latest Evaluations

### Stage 0-1 (2026-07-21)
See [`stage0-1-eval-2026-07-21.md`](stage0-1-eval-2026-07-21.md) — **15 bugs** (4 critical, 5 medium, 6 low)

### Stage 2-3 (2026-07-21)
See [`stage2-3-eval-2026-07-21.md`](stage2-3-eval-2026-07-21.md) — **35 bugs** (11 critical, 14 medium, 10 low)

**Top 3 critical across all stages:**
| ID | Stage | Description |
|----|-------|-------------|
| B-S3-2 | Stage 3 | **Sandbox bypass** — blocklist allows LLM scripts to recover dangerous libs (debug/io) |
| B-S3-4 | Stage 3 | **No mutex on lua_State** — two FreeRTOS tasks race on Lua VM |
| B-S2-4 | Stage 2 | **False dedup** — weak XOR hash silently drops legitimate device data |

### Key Open Issues (from previous evaluations)

- **B4 (old)**: NimBLE config missing from sdkconfig.defaults
- **N5 (old, fixed?)**: VFS leak on storage init partial failure — appears fixed in current code
- **N6 (old)**: Partial write leaves incomplete file

## Archive

- `archive/bug-luminous-spring.md` — Initial evaluation (30 bugs, 2026-07-18)
- `archive/new_bugs_2026-07-19_00-00-58.md` — Incremental findings (6 new, 2026-07-19)
- `archive/bug_fix_report/fix-report-2026-07-19.md` — Fix verification report