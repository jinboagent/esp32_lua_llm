# Stage 6 — Agent Platform (overview)

Stage 6 turns the dongle from "a device an LLM programs" into "a device
an LLM *operates*": capability registration authored on-device in Lua,
consumed by any host. The design principle throughout: the device expert
(the human who knows the hardware) registers tools once; every future
LLM session inherits them as a typed, callable surface.

| ID | Feature | Milestone | State |
|----|---------|-----------|-------|
| H6.1 | [Lua Tool Registry](feature_tool_registry.md) | M1–M4 | implemented + live-verified 2026-09-03→07, branch `Lua_tool_extension_dev` (unmerged, review pending) |

## Milestones (all implemented; details in the feature spec)

- **M1 (host-only):** the `manifest()` pack convention, `/tools`
  registration (confirm-gated), host-side validation, TOOLS prompt
  section, generate-and-execute. Zero firmware.
- **M2:** `pack_store` in LittleFS + `PACK` CLI family + boot autorun —
  the device carries its own tools across power cycles.
- **M3:** `hw.*` device bindings behind a build flag (millis/gpio/adc +
  the `kv` store = configure mode), `LUA BEGIN/END` chunk exec,
  `--mutating-gate` host policy.
- **M4:** `--native-tools` function-calling option (registry → `tools`
  array). Parked by design: GATT exposure, hook chaining.

Process reports + run transcripts: `harness/02-knowledge/`.
