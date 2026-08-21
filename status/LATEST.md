# LATEST — Status Pointer

**Current status:** [`status-2026-08-22-0059.md`](status-2026-08-22-0059.md)

## At a glance

- **F2.4 implemented + verified** on `ble_connected`: optional BLE
  connection (GATT client) — connect by service UUID or address, notify
  or poll a characteristic, re-stream as `"src":"conn"` JSON lines;
  build flag `CONFIG_BLE_CONN_ENABLED` + runtime CONN commands
- **Review amendments absorbed** (zai A1–A6, deepseek D1–D7): CLI state
  matrix, queue+emitter, addr-type learn, 128-bit tap parse, MTU/trunc,
  own mutex, power observability, envelope-wins precedence
- **Verified**: host 95/95; off-build excludes conn; HIL C0 14/14 on
  device; regressions 32/14/45/11 green; GATT data path = manual nRF
  Connect runbook on this PC (WinRT server APIs unavailable)
- **Also in this branch**: architecture-pattern guide
  (`harness/00-global-context/architecture_patterns.md`), both review
  docs, proposal data-flow update
- **Next:** push when permitted; manual runbook; optional re-soak
