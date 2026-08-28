# LATEST — Status Pointer

**Current status:** [`status-2026-08-28-host-cases.md`](status-2026-08-28-host-cases.md) and [`status-2026-08-28-assistant.md`](status-2026-08-28-assistant.md) — stage 5 (host tooling) merged to master 2026-08-28

## At a glance

- **Stage 5 complete on master** (squash-merged 2026-08-28, branches
  kept): H5.1 `llm_loop.py` · H5.2 `host_app/run_case.py` (application
  cases, plant demo with ground-truth-checked LLM estimates) · H5.3
  `host_app/assistant.py` (interactive session, typed envelopes,
  human-confirmed deploys, `--system-extra/--system-file` prompt
  experiments)
- **H5.2 verification:** unit 33/33 · hw 43/0 (2 informed SKIPs) ·
  demo 60/60 conn lines, physics green · τ_est 9.5 vs 10.0
- **H5.3 verification:** unit 42/42 · live clarify round-trip,
  confirmed deploy, device `-612` gate surfaced verbatim, mode
  boundary, `[PILOT]` prompt-override proof (evidence:
  `harness/02-knowledge/evidence-assistant-2026-08-28/`)
- **F2.4 firmware fix in the merge** (source now matches the flashed
  dongle): `CONN TARGET` responses valid JSON in both uuid forms; hw
  suite hardened (response `cmd`-field matching, STOP-settle, C2
  START-rejected = informed SKIP)
- **LLM backend resilience in both tools**: env vars win as a unit;
  `.llm_env` may carry legacy `LLM_*` or provider pairs
  (DASHSCOPE_*/TOKEN_PLAN_* + QWEN_MODEL); 401 auto-fallback to the
  file; `/api/v1` → `/compatible-mode/v1` self-heal (the Aliyun
  console's native-dialect path kept regressing into `.llm_env`)
- **F2.4 GATT data path fix merged earlier the same day** (NimBLE
  discovery dispatch + JSON merge separators); PC-as-GATT-peer verified
  end-to-end
- **Master is the F2.4 product line** (adoption 2026-08-23); history:
  [`status-2026-08-22-improvement-pass.md`](status-2026-08-22-improvement-pass.md)
- **Docs refresh (2026-08-28, same day)**: `CLAUDE.md` rewritten for
  stage 5 (tools table, rules, build recipes incl. the Git-Bash idf.py
  invocation); README status → v1.1.0 with the current test inventory;
  **new** `docs/quickstart.md` (10-minute path) and
  `harness/01-features/stage5-host/README.md` (which-tool-when +
  shared host-tool conventions); build_environment.md gained the
  Git-Bash build section
- **Open**: F2.4 backlog (firmware-side CONN STOP settle; strict-JSON
  sweep over CLI responses); `read_only` case next; optional `v1.1.0`
  stage-5 tag
