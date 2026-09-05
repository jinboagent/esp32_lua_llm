# Feature: Lua Tool Registry (LLM tool registration by the device expert)

## Basic Info

| Field        | Value                                              |
|--------------|----------------------------------------------------|
| Feature ID   | H6.1 (milestone M1)                                |
| Stage        | 6 — Agent platform                                 |
| Layer        | Host (Python, PC side) + a device-side Lua convention |
| Dependencies | F4.1 (CLI, `LUA EXEC`), H5.3 (assistant session)   |
| Source Files | `host_app/assistant.py`, `host_app/tool_packs/demo.lua` |
| Test Files   | `tests/host/test_assistant.py`; live sessions in `harness/02-knowledge/` |
| Origin       | Proposal `docs/feature-proposal-lua-tool-registry-2026-08-29.zcode.md` (decisions 1–14, incl. the deepseek addendum) |
| Status       | Implemented 2026-09-03 (M1), branch `Lua_tool_extension_dev` — unit 79/79 + live evidence `harness/02-knowledge/evidence-tool-registry-2026-09-03/`; unmerged, review pending |

## Functional Description

Lua scripts stop being only "programs the LLM writes" and become
**capability declarations**. A *tool pack* is a Lua file, written by the
person who knows the hardware, that defines callable functions plus a
`manifest()` function returning a JSON description of them (names,
argument types, docs, examples). The host fetches the manifest, injects
it into the LLM's system prompt, and the LLM then *composes the tools*
at runtime by writing Lua programs that are executed on the device
immediately — function-calling for the LLM, with the registry authored
on the device.

### The M1 layers (of the four-layer design)

- **L1 — in-script declaration:** `manifest()` beside the tool
  functions; single source of truth. Pack convention: **every line is
  one complete statement ≤ 240 B** (the `LUA EXEC` line path); private
  globals carry a pack-name prefix; long manifests are assembled by
  string concatenation across lines.
- **L2 — manifest transport:** fetched with
  `LUA EXEC return string.sub(manifest(),i,j)` slices (the 256 B result
  path cannot carry a whole manifest), joined host-side, **validated
  host-side** (decision 9): strict JSON, `version:1`, Lua-safe
  identifier names, well-formed args, no duplicate or reserved names
  (`manifest`/`on_adv`/`transform`).
- **L3 — call channel (generate-and-execute, decisions 7+10):** the
  LLM replies with the existing `lua` envelope whose `code` composes
  tools and ends in `return <string>`. The assistant executes it
  **autonomously** (no per-program confirm — humans act at intent
  level), feeds the result string back, and the LLM gives its final
  answer. Consecutive executions are capped at 3 per user turn.

### Registration and trust (decisions 4, 8, 13, 14)

- `/tools load <file.lua>` is the privileged human act, behind a
  confirm. The host fail-closed scans every line first — the SAME token
  list as the device bridge (`os.`/`io.`/`debug.`/`package.` dotted;
  `dofile`/`loadfile`/`load`/`require`/`collectgarbage` word-bounded) —
  then `LUA EXEC`s each line into the live Lua state (**run mode,
  decision 14**: RAM only, nothing persisted, the flash slot is never
  touched, a running filter script is never evicted).
- Multiple packs are active side by side (their globals coexist);
  cross-pack tool-name collisions are rejected at load (decision 8).
  On-device, a later pack's `manifest()` overwrites the earlier one's —
  so the **host cache is the authoritative registry**; `/tools refresh`
  re-syncs from the device and clears the registry when the device Lua
  state has been reset (reboot / `LUA INIT`).
- Session-scoped by design (decision 13): a reboot clears tools; the
  host pack files are the source of truth and are re-loaded per session.
  Deploy intents (`run`/`configure`/`resident`) are the owner's
  decision 14; M1 ships `run` plus the existing resident deploy path.

### Routing rule

An envelope `code` defining `on_adv`, `transform` or `manifest` is a
filter/pack artifact → the existing deploy path with the human
`deploy? [y/N]` gate. Any other `lua` code while tools are registered
is a tool program → immediate execution. With no tools registered the
old behavior (deploy offer) is unchanged.

### Session surface (additions)

```
/tools                 list registered packs + tools (args, mutating, examples)
/tools refresh         re-read the manifest from the device
/tools load <file.lua> register a pack (confirm-gated; run mode)
```

## Acceptance Criteria

1. `/tools load` registers a pack: host token scan rejects forbidden
   lines before anything is sent; every kept line is a complete
   statement ≤ 240 B; the manifest is fetched in `string.sub` chunks,
   validated host-side, and registered; the user sees the tool listing.
2. Cross-pack name collisions (and reserved names) are rejected before
   any device line is sent; duplicate packs are rejected.
3. `/tools` lists packs and tools; `/tools refresh` updates the matching
   pack and detects a cleared device state (registry reset + guidance).
4. A registered-tools `lua` envelope that composes tools is executed on
   the device autonomously; the result string is shown (`tool> …`) and
   fed back to the LLM, which then answers; at most 3 consecutive
   executions per user turn, after which the LLM is instructed to
   finalize without further execution.
5. A device-side execution error is fed back to the LLM as corrective
   feedback (it may fix and retry within the cap), never crashes.
6. Envelopes defining `on_adv`/`transform`/`manifest` keep the human
   deploy gate; with no tools registered, behavior is exactly H5.3.
7. Every `LUA EXEC` exchange matches responses by `cmd:"lua_exec"`
   (`expected_cmd` extension — the 2026-08-28 stale-line lesson).

## Test Cases

| ID   | Scenario                                       | Expected / result |
|------|------------------------------------------------|-------------------|
| TC-1 | manifest validation (good/bad version/dup/reserved/bad args) | unit suite |
| TC-2 | token-scan parity + line budget + statement splitting | unit suite |
| TC-3 | `/tools load` confirm-gated; `n` sends nothing  | REPL test on DeviceSimSerial |
| TC-4 | tool program round trip: envelope → `LUA EXEC` on the wire → `tool>` result → final answer | REPL test |
| TC-5 | execution cap: 3 programs, then forced final answer | REPL test |
| TC-6 | `on_adv` artifact still deploy-gated            | REPL test |
| TC-7 | chunked manifest fetch + engine-not-ready retry | unit (FakeSerial) |
| TC-8 | live session on COM12 + real LLM: load → list → compose → result → answer | evidence dir |

## Non-Functional Constraints

| Constraint | Requirement                                            |
|-------------|--------------------------------------------------------|
| Deps        | stdlib + pyserial only; single file, no threads (H5.3 rules hold) |
| Safety      | registration human-gated; host fail-closed scan = bridge token parity; device sandbox stays the final floor; tee transcript = audit trail |
| Privacy     | without a key nothing leaves the machine (`--no-llm`)  |
| Evidence    | every run tee'd; verification references archived transcripts |

## Known limits (M1, deliberate)

- `LUA EXEC` lines are not scanned on-device (only bridge uploads are);
  the host scan + the sandbox whitelist (forbidden globals simply do
  not exist at runtime) are the M1 gates.
- Locals do not persist between `LUA EXEC` lines — programs are taught
  to stay on one line ending in `return`.
- Results are strings ≤ 256 B; `manifest()` is last-pack-wins on the
  device (host cache authoritative).
- Manifest fetch assumes `string.sub` slicing (chunk 180 B: 256 B result
  buffer / 512 B TX budget with JSON escaping).

## Future Extensions (M2+, from the proposal)

Firmware `TOOLS LIST`/multi-pack LittleFS storage + boot-time `autorun`
honoring (M2); `hw.*` bindings incl. a device-owned key-value store for
`configure`-mode persistence, wall-clock budgets, optional mutating-only
host gate (M3); native function-calling, GATT exposure, hook chaining
(M4).
