# LATEST — Status Pointer

**Current status:** [`status-2026-08-10-2146.md`](status-2026-08-10-2146.md)

## At a glance

- **Firmware v1.0.0 released** — all 13 features / 4 stages implemented and hardware-verified
- **Bug backlog fully closed again 2026-08-10** — the 2026-08-05 re-evaluation's 7 findings resolved (6 fixed, 1 accepted + documented)
- **Verification:** host tests 67/67 · `test_bridge_hw.py` 32/32 · `test_power_hw.py` 14/14 · `test_script.py` / `test_lua.py` green (COM12)
- **Note:** build cache broke once when the active ESP-IDF version changed between sessions (v5.3.1 cache vs v5.1 env) — delete `build/` to reconfigure
- **Next:** host-side LLM-loop tooling — field-test the full product loop
  (scan JSON → LLM generates Lua → upload via `SCRIPT LOAD`)
