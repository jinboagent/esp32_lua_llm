# LATEST — Status Pointer

**Current status:** [`status-2026-08-05-1355.md`](status-2026-08-05-1355.md)

## At a glance

- **Firmware v1.0.0 released** — all 13 features / 4 stages implemented and hardware-verified
- **Bug backlog fully closed 2026-08-05** — 0 open items across all evaluations
  (2 deferred-by-design + USB suspend + PMIC measurement documented for v2)
- **Verification:** host tests 67/67 · `test_bridge_hw.py` 32/32 · `test_power_hw.py` 14/14 (COM12)
- **Next:** host-side LLM-loop tooling — field-test the full product loop
  (scan JSON → LLM generates Lua → upload via `SCRIPT LOAD`)
