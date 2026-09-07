# Bug Tracking — BLE Sniffer Dongle

> Consolidated index. **2026-08-11**: eval found H1/H2/H3 + 4 stale test
> scripts; all fixed and regression-covered the same day (fix report below).
> Remaining: none — H4 soak completed 2026-08-12, no fragmentation observed.
> Historical evaluation reports linked below; early reports archived in `archive/`.

## Status: H6.1 PRE-MERGE AUDIT OPEN (2026-09-07) — all prior findings resolved ✅

| Evaluation | Bugs | Outcome | Report |
|------------|:----:|---------|--------|
| Initial eval (2026-07-18/19) | 30 + 6 | all fixed (≤ `d69f65e`) | [`archive/bug_fix_report/fix-report-2026-07-19.md`](archive/bug_fix_report/fix-report-2026-07-19.md) |
| Stage evals (2026-07-21) | 66 | superseded by the 2026-08-04 all-stage re-evaluation | [`stage0-1-eval`](stage0-1-eval-2026-07-21.md), [`stage2-3-eval`](stage2-3-eval-2026-07-21.md), [`stage4-eval`](stage4-eval-2026-07-21.md) |
| All-stage (2026-08-04) | 28 remaining | 23 fixed, 2 deferred-by-design, 3 rejected | [`fix-report-2026-08-04.md`](bug_fix_report/fix-report-2026-08-04.md) |
| Stage 4 vs v1.0.0 (2026-08-04) | 16 + module gaps | 14 fixed by Stage 4 implementation, 2 fixed (`e8427c2`), 1 deferred (USB suspend → v2) | [`fix-report-2026-08-04-stage4-vs-v1.0.0.md`](bug_fix_report/fix-report-2026-08-04-stage4-vs-v1.0.0.md) |
| Backlog verification (2026-08-05) | 28 re-verified | **0 genuine remainders** — tracker had gone stale; every item already closed (`1eb7d04`) | [`fix-report-2026-08-05-backlog-verification.md`](bug_fix_report/fix-report-2026-08-05-backlog-verification.md) |
| **v1.0.0 re-eval (2026-08-05)** | **7 found** | **All closed 2026-08-10**: 6 fixed (B1 atomic script flags, B2 PM lock-failure safety, B3 `bridge_init` call, B4 allocator accounting, B6 deinit guard, B7 dedup sentinel), 1 accepted + documented (B5 stats snapshot) | [`stage-all-eval-2026-08-05.md`](stage-all-eval-2026-08-05.md), [`fix-report-2026-08-10-eval-2026-08-05.md`](bug_fix_report/fix-report-2026-08-10-eval-2026-08-05.md) |
| **Manual BLE test (2026-08-10)** | **3 found** | N1 (`ts` always 0) fixed; N2 (scan died after 10.24 s) fixed via `BLE_HS_FOREVER` continuous discovery; N3 characterized — closing the COM port resets the chip (`ESP_RST_USB`, chip behavior; keep the port open) | [`fix-report-2026-08-10-ble-manual-test.md`](bug_fix_report/fix-report-2026-08-10-ble-manual-test.md) |
| **BLE/Lua HW re-eval (2026-08-11)** | **3 + 1 watch** | **OPEN**: H1 invalid JSON in Lua error/result responses (live-proven, incl. SCRIPT END compile errors); H2 overlong-line tail re-parsed as command; H3 512-byte chunk guidance impossible over 255-char USB line; H4 watch: pool fragmentation (needs soak test). Stale hook scripts (T1–T4) superseded by `test_ble_lua_hw.py` (45 checks, 43 pass) + `test_ble_peer_hw.py` (11 checks, 11 pass — PC as controlled BLE peer; v1 non-connectability pinned) | [`stage-all-eval-2026-08-11.md`](stage-all-eval-2026-08-11.md) |
| **H1/H2/H3 fixes (2026-08-11)** | **3 fixed** | **RESOLVED**: `json_escape_str()` + escaped CLI/bridge emit paths (H1); overflow drain-to-EOL with Ctrl+C pushback (H2); 121-byte chunk transport cap documented (H3). Bonus: host-suite build restored (`esp_system.h` shim — broken since N1/N2). Verified: host 76/76, BLE+Lua 45/45, peer 11/11, bridge 32/32, power 14/14 | [`fix-report-2026-08-11-eval-h1-h3.md`](bug_fix_report/fix-report-2026-08-11-eval-h1-h3.md) |
| **Architecture eval (2026-08-15)** | **1 correctness + drift** | **RESPONDED 2026-08-16**: pool allocator rewritten (`lua_pool.c`: uint32 offsets, coalescing, single ledger, 10 host tests incl. >64 KB regression); filter lock decoupled + hook-presence cached; specs annotated (impl notes + test refs); reuse policy annotated. Disagreements argued (AC-4 misread, IDF version, esp_console/registry/rename won't-fix) | [`docs/evaluation-response-2026-08-16.md`](../docs/evaluation-response-2026-08-16.md) |
| **H6.1 branch audit (2026-09-07)** | **2 high + 7 med + 10 low + 4 pre-existing** | **PARTLY FIXED — `6407df2` (deepseek)** closed the two HIGH + two host MED: B1 `PACK END` invalid-JSON escape, B2 fail-open host validation, B4 native cap per-call + round backstop, B8 `c["id"]` guards; verified python 147/147, Unity 148/0. **Still OPEN**: B3 autorun dirent cap (packs vanish at boot), B5 mutating-gate alias bypass, B6 deploy path >255B line drops, B7 Ctrl+C gap, B9 `/tools load @name` clobber, B10–B19 low, P1–P4 pre-existing | [`h61-branch-audit-2026-09-07.zcode.md`](h61-branch-audit-2026-09-07.zcode.md) |

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

## Verification state (2026-08-11, firmware v1.0.0 + N1/N2 + H1/H2/H3)

- host suite: **76/76** (was unbuildable since N1/N2 — `esp_system.h` shim restored it; +9 escape regression tests)
- `test_ble_lua_hw.py`: **45/45** (H1/H2 tracker checks now green)
- `test_ble_peer_hw.py`: **11/11** (capture threshold tuned: ≥2 exact-field lines / 8 s, WinRT adv interval is fixed)
- `test_bridge_hw.py`: **32/32** · `test_power_hw.py`: **14/14**
- H4 soak: **closed 2026-08-12** — 2 h, 24 samples, no creep (early/late avg 20846/21738), no stalls, no resets; `lua_peak` now a true high-water mark

## Verification state (2026-08-11, firmware unchanged v1.0.0 + N1/N2)

- `test_ble_lua_hw.py` (NEW): **43/45** on COM12 — the 2 fails track open
  findings H1/H2
- `test_ble_peer_hw.py` (NEW): **11/11** on COM12 — PC as controlled BLE
  peer (WinRT advertiser); dongle not discoverable (v1 boundary)
- Hook data plane proven on target: 7-arg ABI, suppress-all = 0 lines,
  transform rewrite = 100 % marker, SCRIPT STOP restores stream
- `test_hooks.py` / `test_hooks2.py` / `test_script.py` / `test_bugfixes.py`:
  **superseded — do not trust** (T1–T4, see 2026-08-11 eval)

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
