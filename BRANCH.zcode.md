# BRANCH — H6.1 Lua Tool Registry, M1–M4

> **Author:** zcode · 2026-09-03 (M2–M4 completed 2026-09-07)

- **Branch:** `Lua_tool_extension_dev`
- **Feature:** H6.1 — Lua tool packs (`manifest()` convention) +
  assistant generate-and-execute, across all four roadmap milestones:
  M1 host-only loop · M2 pack persistence + boot autorun · M3 `hw.*`
  device bindings + kv configure-mode + chunked exec + mutating gate ·
  M4 native function-calling option. GATT exposure and hook chaining
  remain parked M4 options by design.
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
- 2026-09-07 **M2** `70e4ed0` (rebased onto advanced master): firmware
  `pack_store` (`/littlefs/packs/`, bridge-parity scan, `PACK`
  CLI family, boot autorun — decision 13's seam honored), storage
  `list_dir`; host `/tools persist [autorun]`, `/tools load @name`,
  device-pack listing. Unity pack suite 12/0 (contract 87); hw
  `test_pack_hw.py` 13/13 incl. THE M2 PROOF (reboot → autorun →
  manifest alive with no host load); gate 6/6; live persist→@demo
  (evidence 05-07)
- 2026-09-07 **M3** `68dd403`: `hw.*` device bindings behind
  CONFIG_LUA_HW_BINDINGS (millis/gpio whitelist/adc/kv — decision 14's
  configure mode made real), LUA BEGIN/END chunk exec (locals persist),
  host chunk path + `--mutating-gate` (decision 10's M3 return);
  hwio.lua 6-tool pack. Unity chunk suite 8/0 (147 total); hw
  `test_hwio_hw.py` 18/18 incl. THE CONFIGURE PROOF (kv survives
  reboot, raw binding AND tool level); gate 6/6; live hw composition +
  gate declined/confirmed (evidence 08-11). Two live-caught fixes:
  GPIO INPUT_OUTPUT read-back; session-vs-parameters after reboot
- 2026-09-07 **M4** `adbd69f`: `--native-tools` — registry as an
  OpenAI tools array, tool_calls through the same device path, tool
  results back as role:tool messages, cap + gate apply; python 144/144;
  LIVE on qwen3.8-27b: native tool_calls executed + answered (evidence
  12). GATT exposure + hook chaining remain parked M4 options by design

## Merge record (filled at merge time)
- squash commit: \<sha\> on master
- FEATURES.zcode.md entry added: \<date\>
