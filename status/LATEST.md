# LATEST — Status Pointer

**Current status:** [`status-2026-08-28-host-cases.md`](status-2026-08-28-host-cases.md)

## At a glance

- **H5.2 host cases complete on `feature/5-host-cases`**:
  `host_app/run_case.py` — pluggable case framework (first_order plant
  demo), `--estimate` LLM act checked against ground truth. Unit 24/24
  · hw 43/0 · demo 60/60 lines · τ_est 9.5 vs 10.0 (qwen3.8-flash)
- **Two F2.4 firmware bugs found & fixed on that branch, wire-verified**:
  two-UUID `CONN TARGET` response was invalid JSON (missing `chr`
  quote — now two explicit emits, C1 asserts the echo); `CONN STOP` ok
  can precede the state flip to `off` (state-settle host-side, firmware
  fix on the backlog). Hw suite hardened: response `cmd`-field matching
  (kills stale-line false passes), C2 START-rejected quirk = informed
  SKIP
- **F2.4 GATT data path fix merged earlier today** (on master): NimBLE
  discovery dispatch + JSON merge separators; PC-as-GATT-peer verified
  end-to-end
- **Master is the F2.4 product line** (adoption 2026-08-23);
  improvement pass + README deep-dive history:
  [`status-2026-08-22-improvement-pass.md`](status-2026-08-22-improvement-pass.md)
- **Open**: review/merge `feature/5-host-cases` and
  `feature/5-assistant` (H5.3); `.llm_env` now uses provider pairs
  (DASHSCOPE/TOKEN_PLAN/QWEN_MODEL) — port the fallback to
  assistant.py; F2.4 backlog (CONN STOP settle, strict-JSON sweep)
