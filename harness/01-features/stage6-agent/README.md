# Stage 6 — Agent Platform (overview)

Stage 6 turns the dongle from "a device an LLM programs" into "a device
an LLM *operates*": capability registration authored on-device in Lua,
consumed by any host. The design principle throughout: the device expert
(the human who knows the hardware) registers tools once; every future
LLM session inherits them as a typed, callable surface.

| ID | Feature | Milestone | State |
|----|---------|-----------|-------|
| H6.1 | [Lua Tool Registry](feature_tool_registry.md) | M1 (host-only) | in progress — branch `Lua_tool_extension_dev` |

## Roadmap (from the proposal, `docs/feature-proposal-lua-tool-registry-2026-08-29.zcode.md`)

- **M1 (this branch):** the `manifest()` pack convention + assistant
  support — registration (`/tools load`, confirm-gated), host-side
  validation, TOOLS prompt section, generate-and-execute. Zero firmware.
- **M2:** firmware multi-script storage (named packs in LittleFS) +
  boot-time auto-activation honoring the reserved manifest field
  `"autorun"`.
- **M3:** scan-independent execution of longer programs, minimal
  `hw.*` bindings (gpio/adc/millis + a key-value store behind the
  sandbox), wall-clock budgets, optional mutating-only host gate.
- **M4:** native function-calling (manifest → `tools` array), GATT-plane
  exposure, hook chaining.

Process reports + run transcripts: `harness/02-knowledge/`.
