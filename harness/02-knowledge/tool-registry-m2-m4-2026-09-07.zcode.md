# Process Report — H6.1 milestones M2–M4 (the registry becomes a platform)

> **Author:** zcode · 2026-09-07 · Branch `Lua_tool_extension_dev`
> (commits `70e4ed0` M2 → `adbd69f` M4, on top of the M1 line incl.
> deepseek's hardening) · Evidence: `evidence-tool-registry-2026-09-03/`
> items 05–12 · Companion: `tool-registry-2026-09-03.zcode.md` (M1)

## What was done

The owner asked to "continue the rest M to the end and test". All three
remaining roadmap milestones, each built → flashed → verified before
the next:

- **M2 — pack persistence + boot autorun (firmware + host).** New
  `pack_store` component (`/littlefs/packs/<name>.lua` +
  `<name>.autorun` markers), the `PACK` CLI family, `storage_list_dir`,
  `bridge_scan_line` exported so packs and filter scripts face ONE
  fail-closed policy, boot autorun wired in `main.c`. Host: `/tools
  persist [autorun]`, `/tools load @name`, device-pack listing. Storage
  stays inert (persist ≠ execute) — decision 14's run/resident split
  kept honest in the device itself.
- **M3 — the device API.** `hw.*` bindings behind
  `CONFIG_LUA_HW_BINDINGS`: millis, gpio (whitelist; USB/flash/PSRAM
  pins excluded), ADC1, and the **kv store** — decision 14's
  configure mode made literal ("the microcontroller takes care of these
  parameters"). `LUA BEGIN … LUA END` uploads a longer program as ONE
  chunk (locals persist). Host: chunk path for multi-line programs +
  `--mutating-gate` (decision 10's gate returns exactly where
  promised: when actuation became physically real). `hwio.lua` pack.
- **M4 — native function-calling (option).** `--native-tools`: the
  registry renders as an OpenAI `tools` array; `tool_calls` execute
  through the same device path; results return as `role:"tool"`
  messages; cap and gate apply unchanged. Verified LIVE on
  qwen3.8-27b — the endpoint accepted the array and the model composed
  `uptime_ms` + `pin_read` natively.

## Decisions made this session

- One scan policy, three surfaces: exporting `bridge_scan_line` beats
  a second token list — packs, chunks and filter scripts are rejected
  by the same bytes.
- Storage is inert by design: `PACK RUN` (or boot autorun) is the only
  path from persisted text to live globals. The M2 hw test pins
  "stored ≠ executed".
- kv = parameters, packs = capabilities: after a reboot the kv value
  answers immediately (the device owns it) while pack globals need
  `PACK RUN` (session-scoped tools, decision 13). The hw suite pins
  BOTH behaviors — this was a live-caught distinction, not a prior
  design.
- GPIO outputs configure as `INPUT_OUTPUT` so written levels read
  back (pure OUTPUT keeps the input buffer off).
- Native mode gets the args serializer; generate-and-execute stays
  serializer-free (decision 7 intact — the two modes receive different
  arg shapes by definition).

## Problems encountered (again: live/hw verification did the finding)

1. GPIO read-back returned 0 after a write (hw suite) — pure OUTPUT
   mode keeps the input buffer off; fixed with INPUT_OUTPUT.
2. The configure-proof test initially failed because after reboot the
   pack FUNCTIONS were gone (session-scoped) while the kv VALUE was
   fine — the test now proves both layers separately, which is the
   actual product distinction.
3. Build-side: `sdkconfig.h` must be included for Kconfig macros in
  component code; the ADC oneshot API lives in `esp_adc`, not the
  legacy `driver/adc.h`; GCC format-truncation on a length-prechecked
  copy (memcpy instead of snprintf).
4. Lua chunk state errors return rc 0 + JSON body (LUA-family
  convention), unlike SCRIPT's CLI_ERR_STATE — pinned by tests.

## Test results

- Unity C host runner: **147 tests green** (pack suite 12, chunk suite
  8; response contract 87 strict-JSON responses)
- Python: assistant **144/144**, run_case 35/35, llm_loop 10/10
- Hardware: `test_pack_hw.py` 13/13 (M2 proof: reboot → autorun →
  manifest alive, no host load) · `test_hwio_hw.py` 18/18 (configure
  proof: kv survives reboot at raw-binding AND tool level) ·
  `run_all_hw.py` 6/6 suites on the reflashed firmware
- Live: 12 archived transcripts (M2 persist→@demo; M3 hw composition,
  gate declined + confirmed; M4 native calls)

## Lessons learned

- The "walking skeleton first" ordering paid off concretely: every M2+
  mechanism (sessions, scan, storage, confirm gates) reused patterns
  M1 had already proven, so the new milestones were mostly assembly of
  known-good parts.
- Milestones that change FIRMWARE must re-run the full hw gate before
  the next milestone builds on the flash — the two reflashes each
  caught environment-level surprises early.
- The configure-mode proof (value outlives the script that set it) is
  the single most convincing demo of decision 14 — worth keeping at
  the top of any future show-and-tell.
