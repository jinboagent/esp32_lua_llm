# Status — H6.1 Lua Tool Registry M1–M4 complete (branch, unmerged)

> **Author:** zcode · 2026-09-07 · Branch `Lua_tool_extension_dev`
> (rebased onto master `d0b73a3`; implementation commits `2b21853`
> M1 → `adbd69f` M4, incl. the deepseek M1 hardening `66c0149`) ·
> Previous: `status-2026-09-03-tool-registry.zcode.md`

## At a glance

- **All four H6.1 milestones implemented and live-verified** on
  `Lua_tool_extension_dev`, awaiting owner review + merge:
  - **M1** (09-03) — the `manifest()` pack convention, `/tools`,
    generate-and-execute (autonomous, cap 3/turn).
  - **M2** (09-07, `70e4ed0`-line) — firmware `pack_store`:
    `/littlefs/packs/` + `PACK LIST/BEGIN/END/RUN/DEL/AUTORUN` + **boot
    autorun** (decision 13's seam honored — hw-proven: tools alive
    after reboot with no host load); host `/tools persist [autorun]`,
    `/tools load @name`.
  - **M3** (`68dd403`) — `hw.*` device bindings behind
    `CONFIG_LUA_HW_BINDINGS` (millis, whitelisted gpio, ADC1, and the
    **kv store = decision 14's configure mode**, hw-proven across a
    reboot), `LUA BEGIN/END` chunk exec (locals persist), host chunk
    path + `--mutating-gate` (decision 10's gate returns as host
    policy); `hwio.lua` 6-tool pack.
  - **M4** (`adbd69f`) — `--native-tools` function-calling option;
    verified LIVE on qwen3.8-27b (native tool_calls executed and
    answered). GATT exposure + hook chaining parked by design.
- Dongle reflashed with M2+M3 firmware; full regression gate green.

## Verification inventory

- Unity host C: **147 tests** green (incl. pack suite 12, lua-chunk
  suite 8; response contract now 87 strict-JSON responses)
- Python: assistant **144/144** · run_case 35/35 · llm_loop 10/10
- Hardware: `test_pack_hw.py` **13/13** (M2 proof) ·
  `test_hwio_hw.py` **18/18** (configure proof) · `run_all_hw.py`
  **6/6 suites** on the new firmware
- Live evidence: 12 transcripts in
  `harness/02-knowledge/evidence-tool-registry-2026-09-03/`
  (M1 loop + bug catches, M2 persist→reboot→@demo, M3 hw composition +
  gate declined/confirmed, M4 native tools)
- Bugs caught by live/hw verification (the point of it): stray manifest
  brace (device -612), `mean` named-field mismatch (nan), GPIO
  read-back needing INPUT_OUTPUT, session-vs-parameters after reboot

## Next

- Owner review → merge ritual (squash, delete `BRANCH.zcode.md` inside
  the merge, add the `FEATURES.zcode.md` entry, keep the branch)
- Optional M1.5 pseudo-push experiment; parked M4 options (GATT
  exposure, hook chaining) keep their recorded triggers
