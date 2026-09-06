# BRANCH — H6.1 Lua Tool Registry, M1 (host-only)

> **Author:** zcode · 2026-09-03

- **Branch:** `Lua_tool_extension_dev`
- **Feature:** H6.1 M1 — Lua tool packs (the `manifest()` convention) +
  assistant generate-and-execute support; zero firmware changes
- **Baseline:** master @ `c9665a7` (2026-09-03) — the docs commit landing
  the converged proposal; the branch was originally cut at `e287261` and
  fast-forwarded to `c9665a7` before its first commit so the spec can
  reference the proposal in-tree
- **Spec / proposal:** `docs/feature-proposal-lua-tool-registry-2026-08-29.zcode.md`
  (decisions 1–14) → promoted spec
  `harness/01-features/stage6-agent/feature_tool_registry.md`
- **Status:** in-progress

## Scope
In: demo pack (`host_app/tool_packs/demo.lua`); assistant `/tools`
commands + host-side manifest validation + TOOLS system-prompt section +
the generate-and-execute autonomous loop (consecutive-execution cap 3);
unit tests; live verification evidence.
Out (deliberately): firmware commands (`TOOLS LIST`/`TOOL CALL` = M2/M3),
`hw.*` bindings, multi-pack flash storage + `autorun` honoring (M2),
structured `tool_call` envelopes (optional later mode), `llm_loop.py`
changes.

## Acceptance criteria
- [x] `/tools load` registers a pack: host fail-closed token scan
      (bridge parity), line-complete statements ≤ 240 B, manifest
      fetched via chunked `LUA EXEC return string.sub(manifest(),…)`
      (256 B result path), validated host-side (decision 9), cross-pack
      name collisions rejected (decision 8)
- [x] `/tools` lists packs/tools; `/tools refresh` re-syncs and clears
      the registry when the device state was reset
- [x] With tools registered, an LLM `lua` envelope composing tools is a
      TOOL PROGRAM: executed autonomously (decision 10 — no per-program
      confirm), result string fed back, loop continues to a final
      answer; consecutive executions capped at 3 per turn
- [x] Envelopes defining `on_adv`/`transform`/`manifest` still take the
      deploy path with the human gate
- [x] `expected_cmd` maps `LUA` → `lua_exec` (stale-line safety on every
      exec exchange)
- [x] Unit suite green (79/79); live session evidence archived under
      `harness/02-knowledge/evidence-tool-registry-2026-09-03/`

## Verification plan
`python tests/host/test_assistant.py`; then a live session on COM12 with
the real LLM (load → list → tool composition → result → final answer),
transcript archived to `harness/02-knowledge/evidence-tool-registry-*/`.
ALL DONE 2026-09-03 — see the process report
`harness/02-knowledge/tool-registry-2026-09-03.zcode.md`.

## Changelog (append-only)
- 2026-09-03 (start) spec promoted to `harness/01-features/stage6-agent/`;
  implementation begins
- 2026-09-03 `70b15f0` M1 implemented (pack + assistant + 29 tests);
  branch rebased onto master `c9665a7` (the docs commit) so the spec
  references the proposal in-tree
- 2026-09-03 live verification on COM12 caught two demo-pack bugs —
  stray brace in the manifest tail (device -612 per-line compile) and a
  mean-implementation convention mismatch (`nan`) — both fixed, pinned
  by a host-side pack-assembly replay test; clean full loop archived
  (evidence 01-04); status: awaiting owner review, unmerged
- 2026-09-06 (deepseek) AC#2 fix: reject tool-pack name collisions and
  duplicate packs BEFORE any device line is sent (host-side
  `assemble_manifest_from_source` + a pre-send gate in `do_tools_load`);
  expanded the suite 79 → 108 (`/tools refresh` branches, empty listing,
  collision/duplicate before-send, manifest assembly, arg/format helpers,
  `exec_tool_program`, validation edges); fixed the `PACK` test fixture to
  follow the demo.lua line convention. Unit 108/108.

## Merge record (filled at merge time)
- squash commit: \<sha\> on master
- FEATURES.zcode.md entry added: \<date\>
