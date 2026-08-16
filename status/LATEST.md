# LATEST — Status Pointer

**Current status:** [`status-2026-08-16-1125.md`](status-2026-08-16-1125.md)

## At a glance

- **BLE connection (GATT client) proposal written — awaiting review**:
  `docs/feature-proposal-ble-conn-2026-08-16.md` on branch `ble_connected`.
  Requirement + decisions, seam analysis, options A/B/C, impact table,
  test plan, open questions. No code changed; harness promotion deferred
  until review sign-off
- **Earlier today**: host LLM loop shipped + live-verified with glm-5.2
  (`llm_loop.py`, `.llm_env`, `docs/example_llm_generated.lua`)
- **Firmware unchanged** — v1.0.0 + pool rewrite + lock decoupling, host
  **86/86**, all HW suites green
- **Next:** review the proposal → promote to harness F2.4 → implement;
  optional 2 h re-soak when the port is free
