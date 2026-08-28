# Feature: Interactive LLM Assistant Session

## Basic Info

| Field        | Value                                              |
|--------------|----------------------------------------------------|
| Feature ID   | H5.3                                               |
| Stage        | 5 — Host tooling                                   |
| Layer        | Host (Python, PC side)                             |
| Dependencies | F4.1 (CLI), F4.2 (bridge upload), F2.4 (conn plane, optional) |
| Source Files | `host_app/assistant.py`                            |
| Test Files   | `tests/host/test_assistant.py` (21 unit tests); live scripted sessions in `harness/02-knowledge/evidence-assistant-2026-08-28/` |
| Origin       | Proposal `docs/feature-proposal-assistant-session-2026-08-28.md` + review `docs/review-h5-host-tooling-2026-08-28-deepseek.md` |
| Status       | Implemented 2026-08-28, branch `feature/5-assistant` |

## Functional Description

Human-in-the-loop session tool: an operator steers a cloud LLM with free
text while the dongle keeps streaming JSON; the LLM answers, asks for
clarification, or produces a Lua artifact that is deployed only on an
explicit human confirmation. Either party (user *or* LLM) may steer —
mixed-initiative interaction.

```
ESP32 ──USB JSON lines──┐
                        ▼
user ──console input─▶ message Prompt ──▶ context assembly ──▶ cloud LLM
                        │ ingress         (history + snapshot)      │
                        │ validation                                  ▼
                        │ bounded buffers                    typed JSON envelope
                        └─ typed-reply routing ◀────────────────────┘
                              answer | clarify | error | lua → deploy? [y/N]
```

### The message Prompt (named component)

One event loop multiplexing two inputs; responsibilities (all
AC-level):

1. **Continuous drain** — the serial port is drained while the user
   thinks (keep-port-open, N3). Stdin is polled **non-blocking**:
   `msvcrt.kbhit()/getwch()` on Windows, `select()` on POSIX. A
   blocking `input()` is the rejected design (review R1). A piped
   stdin (scripted sessions) is read ahead instead.
2. **Ingress validation** — only lines that parse as JSON enter the
   buffers; invalid lines are dropped and counted, never crash.
   Classification by fields: `src:"conn"` → conn plane, `addr` → adv
   plane, `status` → command response.
3. **Bounded rolling buffer** — per-plane caps: adv sampled
   (`ADV_BUF_MAX=60`), conn kept recent (`CONN_BUF_MAX=200`).
4. **Conversation history** — bounded by a character (token-proxy)
   budget (`HISTORY_CHAR_BUDGET=20000`), trimmed oldest-first.
5. **Context assembly** — one request per turn: system prompt + trimmed
   history + reduced device snapshot + user prompt.
6. **Typed-reply routing** — dispatch of the envelope below.

### Typed JSON response contract (mandatory)

```
{"type": "answer",  "text": "..."}
{"type": "lua",     "text": "why", "code": "-- Lua 5.4 source"}
{"type": "clarify", "text": "question for the user"}
{"type": "error",   "text": "..."}
```

Parse failures get ONE retry with an escaping-focused corrective
instruction (`lua.code` is the highest-risk field — raw newlines and
quotes inside a JSON string), then a fenced ` ```lua ` extraction
fallback, then a clear error to the user. Never a crash (review Rec1).

### Mode boundary (system-prompt requirement)

Adv plane → Lua artifacts allowed (`on_adv`/`transform` hooks). Conn
plane (`src:"conn"`) → **analysis only**; conn lines deliberately
bypass the Lua hooks (F2.4 design). The LLM must not offer Lua for
conn-plane requests. Verified live: the model answers "No — connection
lines bypass the Lua sandbox" when asked for a conn-plane transform.

### Session surface

```
python host_app/assistant.py [port] [--no-llm]
                              [--system-extra FILE] [--system-file FILE]

you: <free text — tasking for the LLM>
llm: (answer | lua artifact + deploy? [y/N] | clarify question | error)
/samples [n]     /scan on|off     /conn on|off|status|target
/deploy          /history         /help  /quit    Ctrl+C = stop + cleanup
```

`-451` on `CONN` commands reports "conn not enabled on this firmware"
(review R2, unit-tested). Every run is tee'd to
`assistant_<timestamp>.log` in the working directory.

**Prompt experiments** (the human-in-the-loop test surface):
`--system-extra FILE` appends to the built-in system prompt — steer
style/persona/behavior without losing the envelope contract;
`--system-file FILE` replaces it outright (the typed-envelope router
stays active, so non-envelope replies surface as clean errors, never a
crash). The banner records which prompt is active. Verified live: an
extra prompt demanding a `[PILOT]` prefix produced
`llm: [PILOT] …` through the qwen3.8-flash/DashScope pair.

### LLM backend

Same conventions as llm_loop.py (OpenAI-compatible chat completions,
urllib only, temperature 0.2, timeout 120 s), with ONE deliberate
difference: real env vars win as a **unit** — if any of
`LLM_BASE_URL`/`LLM_API_KEY`/`OPENAI_API_KEY`/`LLM_MODEL` is set, the
whole config comes from the environment; otherwise the `.llm_env` file
(next to the script, then repo root) is used as-is. The file may carry
the legacy `LLM_*` triple OR provider pairs (`DASHSCOPE_*`, then
`TOKEN_PLAN_*` — first complete pair wins; model from `LLM_MODEL` or
`QWEN_MODEL`), the same rule as run_case.py, ported 2026-08-28 when
`.llm_env` moved to the pair scheme. Per-variable mixing
let an ambient foreign-provider key produce a 401 against the file's
base URL on 2026-08-28; unit resolution closes that class of failure.

## Acceptance Criteria

1. Message Prompt drains the device continuously; stdin polling never
   blocks the loop; Ctrl+C leaves the device idle and closes the port
   only at exit (N3).
2. Every LLM reply parses as a typed envelope or is retried
   (escaping-focused) / recovered (fenced lua) / reported as a clean
   error — never a crash, session always continues.
3. `clarify` round-trips work: the LLM can ask the user, the user
   answers, the exchange completes (mixed-initiative AC).
4. Lua artifacts deploy only after an explicit `y`; the device's
   fail-closed per-line sandbox scan remains the final gate and its
   `-612` reason is surfaced verbatim (mid-upload rejection watch).
5. `/conn` degrades gracefully on feature-off firmware (`-451` →
   "conn not enabled on this firmware"), never hangs.
6. History and device buffers are bounded; the context request is
   assembled from exactly: system prompt + trimmed history + snapshot +
   user prompt.
7. No imports from `llm_loop.py` or H5.2 files (self-contained; house
   style accepts the duplicated helpers).

## Test Cases

| ID   | Scenario                                   | Expected / result |
|------|--------------------------------------------|-------------------|
| TC-1 | `--help`, `--no-llm` piped session          | works without key; prompts refused locally (evidence `…190255.log`) |
| TC-2 | ambiguous prompt → clarify → follow-up      | clarify round-trip completes (evidence `…190818.log`) |
| TC-3 | lua offer → `n`                             | artifact kept, `/deploy` re-offers (`…190900.log`) |
| TC-4 | lua offer → `y`                             | SCRIPT LOAD/END/RUN, filtered stream observed (`…191206.log`) |
| TC-5 | forbidden-token upload                      | device `-612` verbatim, session continues (`minus-612-probe.txt`) |
| TC-6 | conn-plane request                          | answer, never a lua offer (`…191206.log`) |
| TC-7 | unit suite                                  | 21/21 (`python tests/host/test_assistant.py`) |

## Non-Functional Constraints

| Constraint  | Requirement                                            |
|-------------|--------------------------------------------------------|
| Deps        | stdlib + pyserial only; single file, no threads         |
| Safety      | deploy behind human confirmation + device sandbox gate  |
| Privacy     | without a key nothing leaves the machine (`--no-llm`)   |
| Evidence    | tee transcript per run; cleanup outcomes always printed |

## Future Extensions (out of v1)

- `request_data` envelope type — LLM pulls data on demand (v2).
- `/deploy <file.lua>` from disk (low priority).
- Live `-451` check against a `CONFIG_BLE_CONN_ENABLED=n` build (the
  response literal is covered by unit tests and the on/off hw builds).
- llm_loop.py shares the latent mid-upload `-612` blind spot; converge
  both on the fixed upload pattern (with Rec3's canonical-copy rule).
