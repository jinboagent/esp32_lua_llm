# Bug Tracking — BLE Sniffer Dongle

> Consolidated index. **All reported bugs resolved as of 2026-08-10** (firmware v1.0.0).
> Historical evaluation reports linked below; early reports archived in `archive/`.

## Status: ALL BACKLOGS CLOSED ✅

| Evaluation | Bugs | Outcome | Report |
|------------|:----:|---------|--------|
| Initial eval (2026-07-18/19) | 30 + 6 | all fixed (≤ `d69f65e`) | [`archive/bug_fix_report/fix-report-2026-07-19.md`](archive/bug_fix_report/fix-report-2026-07-19.md) |
| Stage evals (2026-07-21) | 66 | superseded by the 2026-08-04 all-stage re-evaluation | [`stage0-1-eval`](stage0-1-eval-2026-07-21.md), [`stage2-3-eval`](stage2-3-eval-2026-07-21.md), [`stage4-eval`](stage4-eval-2026-07-21.md) |
| All-stage (2026-08-04) | 28 remaining | 23 fixed, 2 deferred-by-design, 3 rejected | [`fix-report-2026-08-04.md`](bug_fix_report/fix-report-2026-08-04.md) |
| Stage 4 vs v1.0.0 (2026-08-04) | 16 + module gaps | 14 fixed by Stage 4 implementation, 2 fixed (`e8427c2`), 1 deferred (USB suspend → v2) | [`fix-report-2026-08-04-stage4-vs-v1.0.0.md`](bug_fix_report/fix-report-2026-08-04-stage4-vs-v1.0.0.md) |
| Backlog verification (2026-08-05) | 28 re-verified | **0 genuine remainders** — tracker had gone stale; every item already closed (`1eb7d04`) | [`fix-report-2026-08-05-backlog-verification.md`](bug_fix_report/fix-report-2026-08-05-backlog-verification.md) |
| **v1.0.0 re-eval (2026-08-05)** | **7 found** | **All closed 2026-08-10**: 6 fixed (B1 atomic script flags, B2 PM lock-failure safety, B3 `bridge_init` call, B4 allocator accounting, B6 deinit guard, B7 dedup sentinel), 1 accepted + documented (B5 stats snapshot) | [`stage-all-eval-2026-08-05.md`](stage-all-eval-2026-08-05.md), [`fix-report-2026-08-10-eval-2026-08-05.md`](bug_fix_report/fix-report-2026-08-10-eval-2026-08-05.md) |
| **Manual BLE test (2026-08-10)** | **3 found** | N1 (`ts` always 0) fixed; N2 (scan died after 10.24 s) fixed via `BLE_HS_FOREVER` continuous discovery; N3 characterized — closing the COM port resets the chip (`ESP_RST_USB`, chip behavior; keep the port open) | [`fix-report-2026-08-10-ble-manual-test.md`](bug_fix_report/fix-report-2026-08-10-ble-manual-test.md) |

## Deferred to v2 (documented, accepted)

| Item | Reason |
|------|--------|
| USB suspend detection | USB-Serial/JTAG exposes no bus-suspend signal on this hardware |
| **COM-port-close reset (N3)** | Closing the host COM port resets the chip (`ESP_RST_USB`, reset_reason 11) — ESP32-S3 USB-Serial/JTAG behavior when the host releases the interface. Host tools must keep the port open (normal collector mode). STATUS reports `reset_reason` for field diagnostics |
| PMIC current measurement | `POWER STATUS` current figures are firmware estimates |
| M-S2-3 scan-queue deletion / M-S2-5 `pipeline_deinit` | Lifecycle by design (queue reused + `xQueueReset`; infinite-loop task) |

## Known divergences (spec vs code, decisions pending)

1. `transform` hook signature — spec: `transform(addr, parsed_table)`; impl: `transform(addr, json_string)`. Impl kept (friendlier for LLM-generated scripts). Needs an explicit spec-vs-code decision.
2. `lua_engine_deinit` vs concurrent lock waiters — the race window is now closed in practice: the CLI refuses `LUA DEINIT` while scanning or a script runs (`-911`, B6 fix 2026-08-10). A full refcount-based fix remains a v2 candidate.

## Verification state (2026-08-10, firmware v1.0.0 + N1/N2 fixes)

- Host tests: **67/67** (adv parser, JSON encoder, filter, CLI, bridge)
- `test_bridge_hw.py`: **32/32** on COM12
- `test_power_hw.py`: **14/14** on COM12
- `capture_25s.py` (continuous-scan check): **285 adv in 25 s, ts past 25 s** — no window death, no crashes
- `test_script.py` / `test_lua.py`: state guards, sandbox and timeout behavior as specified

## Archive

- `archive/bug-luminous-spring.md` — Initial evaluation (30 bugs, 2026-07-18)
- `archive/new_bugs_2026-07-19_00-00-58.md` — Incremental findings (6 new, 2026-07-19)
- `archive/bug_fix_report/fix-report-2026-07-19.md` — Fix verification report
