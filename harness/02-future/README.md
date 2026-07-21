# Future Feature Specifications (Stage 2-4)

> ⚠️ **These files are intentionally NOT auto-loaded as LLM context.**
>
> They describe features not yet implemented. Loading them in every session would
> waste ~40% of the context window (~1,742 lines / ~88 KB across 8 files).
>
> When work begins on a new stage, move the relevant files back to `harness/01-features/`.

## Contents

| Stage | Files | Status |
|-------|-------|--------|
| **Stage 2** — BLE Core | `feature_ble_scan.md`, `feature_nimble_init.md`, `feature_scan_pipeline.md` | Not started |
| **Stage 3** — Lua | `feature_lua_port.md`, `feature_lua_script_mgmt.md` | Not started |
| **Stage 4** — Integration | `feature_cli_commands.md`, `feature_lua_llm_bridge.md`, `feature_power_management.md` | Not started |

## How to Activate a Stage

When you're ready to implement Stage N:

```bash
mv harness/02-future/stage<N>-*/ harness/01-features/
```

This makes the feature docs visible to the LLM context loader. After implementation,
consider whether to keep the doc in `01-features/` (if still useful for reference)
or move it back.
