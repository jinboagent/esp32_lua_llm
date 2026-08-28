# Stage 5 — Host Tooling (overview)

Three self-contained PC-side tools close the product loop: the dongle
streams JSON over USB; the host tools analyze it with a cloud LLM and
deploy Lua scripts back. All three speak the same conventions (below)
but share **no code** — deliberate, so parallel AI sessions can build
features on disjoint branches.

| ID | Tool | Shape | Use when |
|----|------|-------|----------|
| H5.1 | [`llm_loop.py`](../../../llm_loop.py) | one-shot CLI (`capture`/`analyze`/`deploy`/`loop`) | scripted runs; `--dry-run` needs no key/network |
| H5.2 | [`host_app/run_case.py`](../../../host_app/run_case.py) | pluggable case framework + runner | verifying/demoing the conn plane with checkable ground truth |
| H5.3 | [`host_app/assistant.py`](../../../host_app/assistant.py) | interactive session (message-Prompt REPL) | human-in-the-loop analysis; prompt experiments |

Specs: [H5.1](feature_llm_loop_tool.md) · [H5.2](feature_host_cases.md)
· [H5.3](feature_assistant_session.md). Process reports + run
transcripts: `harness/02-knowledge/`.

## Which tool when

- **"Show me the loop working"** → `llm_loop.py … loop --dry-run`
  (zero setup) or `… loop` with a key (real LLM).
- **"Prove the connection plane with numbers"** → `run_case.py
  --case first_order --estimate` — the LLM's τ estimate is *checked*
  against the configured plant (pass/fail, not vibes).
- **"I want to steer with my own prompts"** → `assistant.py`; bring
  steering text via `--system-extra FILE` (keeps the reply contract) or
  replace the prompt outright with `--system-file FILE` (replies that
  drop the envelope surface as clean errors, never crashes).
- **"I'm adding a new data source/demo"** → write a `Case` (one class +
  one line in `CASES`); the runner lifecycle never changes. The
  designated next case is `read_only` (the dongle's poll path).

## Shared conventions (copied, not imported)

- **Serial (N3)**: the port stays open for the whole session; closing
  COM12 resets the chip. Cleanup (`SCRIPT/SCAN/CONN STOP`) runs on
  every exit path.
- **LLM config resolution**: real env vars win as a *unit* (any of
  `LLM_BASE_URL`/`LLM_API_KEY`/`OPENAI_API_KEY`/`LLM_MODEL` set → the
  whole config comes from the env); otherwise `.llm_env` (next to the
  script, then repo root) is used as-is — either the legacy `LLM_*`
  triple or provider pairs (`DASHSCOPE_*`, then `TOKEN_PLAN_*`; model
  `LLM_MODEL`/`QWEN_MODEL`). Mixing per-variable is avoided on purpose:
  a foreign provider's ambient key + this file's base URL is a
  guaranteed 401.
- **Two self-heals** (both announced in the transcript, both
  session-sticky): a 401 with an env-sourced key falls back once to the
  file config; a 404 from a base ending `/api/v1` retries once on the
  same host's `/compatible-mode/v1` (the Aliyun console hands out the
  native-dialect path).
- **Deploy is human-gated** (`deploy? [y/N]`), and the device sandbox
  scan is the final gate: the bridge rejects a violating line
  **mid-upload** with `-612` — surfaced verbatim, never masked by the
  `-611` that a later `SCRIPT END` would return.
- **Mode boundary**: advertisement lines may produce Lua artifacts
  (`on_adv`/`transform` hooks); conn lines (`src:"conn"`) are
  analysis-only — they bypass the hooks by design.
- **Evidence**: every run tee's a transcript; verification claims
  reference archived transcripts in `harness/02-knowledge/`.

## Known gaps / next work

- `read_only` case — the dongle poll path is the only data path no case
  covers.
- `llm_loop.py` still has the latent mid-upload `-612` blind spot (its
  `upload_script` never reads during the paced send); convergence
  refactor when touched — canonical copy for shared helpers is
  `llm_loop.py` per review Rec3.
- `--peer external` axis for real sensors (per-case UUIDs already in
  the Case contract).
- `request_data` envelope type for the assistant (LLM pulls data on
  demand) — v2.
