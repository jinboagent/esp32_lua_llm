# Process Report — H6.1 M1 Lua Tool Registry (first implementation session)

> **Author:** zcode · 2026-09-03 (session logs timestamped 2026-09-05 by
> the machine clock) · Branch `Lua_tool_extension_dev` · Evidence:
> `evidence-tool-registry-2026-09-03/`

## What was done

The M1 milestone of the tool registry, exactly per the converged proposal
(decisions 1–14), zero firmware changes:

- `host_app/tool_packs/demo.lua` — the canonical pack: 3 advertised
  tools (`mean` table-arg, `temp_convert` enum-arg, `bench_reset`
  mutating/ack) + 1 private helper; one complete statement per line,
  ≤ 240 B; manifest assembled by `[[..]]` concatenation.
- `host_app/assistant.py` — `/tools`, `/tools refresh`, `/tools load`
  (confirm-gated registration); host fail-closed scan with bridge token
  parity; chunked manifest fetch (`string.sub` slices; the 256 B result
  path cannot carry a manifest); host-side manifest validation;
  cross-pack collision rejection; TOOLS system-prompt section; the
  generate-and-execute loop (autonomous execution, result fed back,
  3-per-turn cap, corrective feedback on device errors); hook/manifest
  artifacts keep the human deploy gate; `expected_cmd` now maps
  `LUA → lua_exec`.
- Tests: `test_assistant.py` 49 → 79 tests, all green; run_case 35/35
  and llm_loop 10/10 unchanged.

## Decisions made this session

- **Host-side scan = bridge token parity** (same dotted + word lists as
  `lua_llm_bridge.c`) rather than a new list: packs and filter scripts
  face one policy; the device sandbox remains the runtime floor since
  `LUA EXEC` is not scanned on-device.
- **Manifest chunk = 180 B**: 256 B result buffer / 512 B TX budget with
  JSON escaping headroom.
- **Duplicate-pack error precedes collision error** in
  `ToolRegistry.add` — the more precise message first.
- **Tool-convention pin**: every tool takes ONE table argument carrying
  its manifest-declared named fields (`mean({numbers={3,5,10}})`) — see
  bug 2 below; the demo pack and the TOOLS prompt both teach it.

## Problems encountered (both caught by live verification — its purpose)

1. **Stray brace in the demo pack's manifest tail** — the final
   concatenation line closed the Lua long-string one bracket early
   (`}}]]}` instead of `}}]}]]`), leaving a trailing `}` that the
   device's per-line compile rejected with `-612 unexpected symbol
   near '}'` (evidence 01). The unit suite could not catch this (no
   Lua on the host). Fix + regression test that replays the pack's
   `DEMO_M` assembly host-side and validates the assembled manifest.
2. **Convention mismatch inside the demo pack** — the manifest declares
   `mean(numbers)`, but the implementation read the table as a bare
   list; the live LLM correctly called `mean({numbers={3,5,10}})` and
   got `nan` (evidence 02: `tool> nan 212.0F`). Fix: `mean` reads
   `a.numbers`, matching how `temp_convert` already read named fields.
   Evidence 03 shows the clean loop: `tool> 6.00 212.0F` → "Mean of 3,
   5, 10 is 6.00; 100 °C is 212.0 °F."

Both bugs are the M1 walking skeleton doing its job: the device's
fail-closed exec path and a real LLM surfaced what host-only tests
structurally cannot.

## Live verification (COM12, current firmware, qwen3.8-27b via .llm_env)

- Load: 18 LUA EXEC lines, confirm-gated, `pack 'demo' registered: 3
  tools` — while a running filter script and the flash slot stay
  untouched (run mode).
- Generate-and-execute: the LLM composed both tools in ONE program,
  executed autonomously, result fed back, correct final answer.
- Mutating tool: `bench_reset` executed autonomously → `ok: bench reset`
  (per decision 10 the mutating flag annotates; gating returns as a
  host policy option at M3).
- Fail-closed scan: a pack containing `os.time()` rejected host-side,
  nothing sent (evidence 04).
- Self-heals observed live: ambient-shell-key 401 → `.llm_env` fallback
  (announced), as designed.

## Test results

- `python tests/host/test_assistant.py` → 79/79 OK
- `python tests/host/test_run_case.py` → 35/35 OK
- `python tests/host/test_llm_loop.py` → 10/10 OK
- Live sessions 01–04 above (transcripts archived verbatim)

## Lessons learned

- The "one complete statement per line" pack convention makes
  host-side replay tests of string assembly genuinely valuable — they
  are the only pre-device guard for exactly the class of bug the
  device caught.
- Manifest arg names are a BINDING contract: the implementer of a tool
  must read the named fields of the single table argument, exactly as
  the manifest declares them. Worth stating in the convention doc the
  moment more packs exist.
- The 401 self-heal + base-path heal fired in every live session on
  this machine — they are load-bearing, not decoration.
