# Status — 2026-08-28: H5.2 host cases done; two F2.4 firmware bugs found & fixed

## At a glance

- **H5.2 complete on `feature/5-host-cases`** (parallel to H5.3's
  branch, disjoint files): `host_app/run_case.py` — pluggable case
  framework, first_order plant demo, `--estimate` LLM act with ground
  truth. **Unit 24/24 · hw suite 43/0 (2 informed SKIPs) · live demo
  60/60 lines with all physics checks green · τ_est 9.5 vs configured
  10.0 (5 % error, qwen3.8-flash).**
- **Two F2.4 firmware bugs found by the first demo attempt, fixed on
  the same branch, wire-verified, hw suite green after**:
  - **BUG A**: two-UUID `CONN TARGET` ok-response was invalid JSON
    (missing `chr` closing quote — a conditional format string cannot
    quote one branch only; the first repair attempt then doubled the
    single-UUID quote, caught by the suite). Now two explicit emits;
    C1 asserts the `svc`/`chr` echo.
  - **BUG B**: `CONN STOP` ok can precede the state flip to `off`
    (-452 race on an immediate TARGET). State-settle waits added
    (runner + suite); firmware-side fix left as an F2.4 backlog note.
  - **BUG B'**: hw-suite response reader now matches the `cmd` field
    to the issued command (stale-line false passes excluded — master's
    committed 43/0 C1 TARGET pass is not reproducible against a
    master-content binary and is now considered a false pass); C2's
    START-rejected quirk shape is an informed SKIP.
- **Runner defects found during live runs and fixed**: tick scheduler
  mixed elapsed/absolute monotonic units (zero notifies sent); config
  resolver extended to the new `.llm_env` provider-pair scheme
  (`DASHSCOPE_*` → `TOKEN_PLAN_*` → legacy `LLM_*`, model
  `LLM_MODEL`/`QWEN_MODEL`). H5.3's assistant still speaks only the
  legacy triple — port the fallback when that branch is next touched.
- **Dongle USB incident**: COM12 dropped off the bus after the
  post-flash suite run and **re-enumerated on its own** minutes later;
  no replug was needed.
- **Also today (same session, earlier)**: `.llm_env` corrected to the
  MaaS `/compatible-mode/v1` path; H5.3 assistant re-verified live.

## Evidence

- `harness/02-knowledge/host-cases-2026-08-28.md` (process report),
  `evidence-host-cases-2026-08-28/` (demo attempt 1: BUG A; attempt 2:
  tick bug; final green demo-run.txt with the estimate act)
- `hw_conn_out.txt` — final suite 43/0 transcript
- `vendor_reference/` remains untracked (user's own addition)

## Next

1. Review + squash-merge `feature/5-host-cases` and `feature/5-assistant`
   (H5.3) per house workflow; keep the branches.
2. F2.4 backlog: firmware-side CONN STOP settle; consider a C0-level
   strict-JSON assertion sweep over all CLI responses.
3. Port the provider-pair config fallback to `host_app/assistant.py`.
