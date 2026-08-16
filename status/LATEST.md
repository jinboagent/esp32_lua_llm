# LATEST — Status Pointer

**Current status:** [`status-2026-08-16-0357.md`](status-2026-08-16-0357.md)

## At a glance

- **LLM loop host tool landed**: `llm_loop.py` closes the product loop on the
  PC — capture adv JSON → LLM writes Lua → deploy via `SCRIPT LOAD` → verify.
  Spec: `harness/01-features/stage5-host/feature_llm_loop_tool.md`
- **Env-configured backend**: `LLM_BASE_URL` / `LLM_API_KEY` / `LLM_MODEL`
  (OpenAI, DeepSeek, OpenRouter, Ollama); `--dry-run` needs no key
- **Field-tested on COM12**: dry-run loop 69→18 adv/6 s; deploy valid 48→11;
  broken Lua surfaces bridge `-612`; capture/analyze offline paths green
- **Firmware unchanged** — v1.0.0 + pool rewrite + lock decoupling, host
  **86/86**, all HW suites green
- **Kept:** commit-message hook enforced (`.githooks/commit-msg`); closing
  the COM port resets the chip (N3)
- **Next:** live-LLM `loop` run (set env vars); optional 2 h re-soak
