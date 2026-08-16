# LATEST — Status Pointer

**Current status:** [`status-2026-08-16-0428.md`](status-2026-08-16-0428.md)

## At a glance

- **Live LLM loop verified**: `llm_loop.py` ran end-to-end against DashScope
  `glm-5.2` — model-generated Lua deployed on first try, stream 68 → 10
  adv/6 s; example script saved at `docs/example_llm_generated.lua`
- **`.llm_env` fallback**: gitignored credential file next to `llm_loop.py`
  (real env vars win) — terminal env never reaches other shells on Windows
- **Product loop fully closed**: scan → JSON → LLM → Lua → deploy → clean
  stream, one command: `python llm_loop.py COM12 loop`
- **Firmware unchanged** — v1.0.0 + pool rewrite + lock decoupling, host
  **86/86**, all HW suites green
- **Next:** push (2 commits ahead); optional 2 h re-soak; v2 candidates
