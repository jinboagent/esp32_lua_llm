# LATEST — Status Pointer

**Current status:** [`status-2026-08-10-2350.md`](status-2026-08-10-2350.md)

## At a glance

- **Firmware v1.0.0 + manual-BLE-test fixes (N1/N2)** — continuous BLE scanning verified past 25 s, all suites green
- **Manual BLE test findings closed 2026-08-10**: N1 timestamps fixed, N2 continuous discovery (`BLE_HS_FOREVER`), N3 characterized (COM-port-close = `ESP_RST_USB` chip reset — keep the port open; `reset_reason` now in STATUS)
- **Verification:** bridge 32/32 · power 14/14 · `capture_25s.py` 285 adv/25 s · host 67/67 (COM12)
- **Note:** `test_ble.py`'s open/close-per-command pattern resets the device (N3) — use open-port tools (`capture_25s.py`) for long-running checks
- **Next:** host-side LLM-loop tooling — field-test the full product loop
  (scan JSON → LLM generates Lua → upload via `SCRIPT LOAD`)
