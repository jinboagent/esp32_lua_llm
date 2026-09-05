# Status — H6.1 M1 Lua Tool Registry implemented (branch, unmerged)

> **Author:** zcode · 2026-09-03 · Branch `Lua_tool_extension_dev`
> (rebased onto master `c9665a7`, implementation commit `70b15f0` + fix
> commit) · Previous: `status-2026-08-28-host-cases.md`,
> `status-2026-08-28-assistant.md`

## At a glance

- **H6.1 M1 (host-only, zero firmware) implemented and live-verified**
  on `Lua_tool_extension_dev`, awaiting owner review + merge: the Lua
  tool-pack convention (`manifest()` beside the tools), the demo pack
  (`host_app/tool_packs/demo.lua`), and assistant support — `/tools`,
  `/tools refresh`, `/tools load` (confirm-gated registration), a TOOLS
  system-prompt section, and the **generate-and-execute** loop (the LLM
  composes registered tools into a Lua program; executed autonomously
  on the device; result fed back; capped at 3 executions per turn).
- **Design first**: 14 locked decisions in
  `docs/feature-proposal-lua-tool-registry-2026-08-29.zcode.md`
  (d14 = deploy modes `run`/`configure`/`resident`), zero open
  questions; spec promoted to `harness/01-features/stage6-agent/`.
- **New project workflow adopted 2026-09-03** (docs on master):
  feature branches cut from registered baselines carry a
  `BRANCH.zcode.md` manifest; master maintains `FEATURES.zcode.md`.
  This is the first feature under it.

## Verification

- `python tests/host/test_assistant.py` → **79/79** (was 49; +30 incl.
  bridge-parity scan, line budget, manifest validation, whole-REPL tool
  flows, execution cap, corrective feedback)
- `python tests/host/test_run_case.py` 35/35 · `test_llm_loop.py`
  10/10 (no regressions)
- Live COM12 + qwen3.8-27b: load → list → composed program →
  `tool> 6.00 212.0F` → correct final answer; mutating `bench_reset`
  executed autonomously; forbidden `os.` pack rejected host-side
  before anything was sent; 401 self-heal fired (announced). Evidence:
  `harness/02-knowledge/evidence-tool-registry-2026-09-03/`
  (4 transcripts — the first two contain the bugs live verification
  caught: stray brace → device -612 per-line compile; `mean`
  convention mismatch → `nan`).

## Next

- Owner review → merge ritual (squash, delete `BRANCH.zcode.md` inside
  the merge, add the `FEATURES.zcode.md` entry, keep the branch)
- M2 candidates per roadmap: firmware multi-pack LittleFS storage +
  boot-time `autorun` honoring; M1.5 pseudo-push experiment
- Standing notes: dangling H4 commit chain (`4d2d9c0..844f62c`) is NOT
  on any branch of this clone (recoverable by sha if wanted)
