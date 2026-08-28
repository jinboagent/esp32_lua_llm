# Process Report — H5.3 Interactive LLM Assistant Session

| Field    | Value                                              |
|----------|----------------------------------------------------|
| Date     | 2026-08-28                                         |
| Branch   | `feature/5-assistant`                              |
| Feature  | H5.3 (proposal: `docs/feature-proposal-assistant-session-2026-08-28.md`, spec: `harness/01-features/stage5-host/feature_assistant_session.md`) |
| Result   | Implemented and verified — unit 21/21, live §6 checks green |

## What was done

Implemented `host_app/assistant.py` (self-contained, stdlib + pyserial,
~560 lines) per the amended H5.3 proposal:

- **Message Prompt** — one event loop multiplexing the serial port and
  the console. Stdin is non-blocking (`msvcrt.kbhit/getwch` on Windows,
  `select` on POSIX, R1); a piped stdin (scripted sessions) is read
  ahead so verification transcripts can be scripted. The loop drains
  the serial port between keystrokes (N3 keep-open honored).
- **Typed envelope** `answer|lua|clarify|error`, defensively parsed;
  one retry with an escaping-focused corrective instruction, then the
  fenced ` ```lua ` fallback (Rec1). Never crashes on a bad reply.
- **Bounded state** — per-plane rolling buffers (adv sampled, conn kept
  recent, R3), conversation history trimmed by a 20 000-char
  (~5 000-token) budget from the oldest end.
- **Context assembly** — system prompt (mode boundary §2.6 + sandbox
  ABI + envelope contract) + trimmed history + reduced device snapshot
  + user prompt, ONE request per turn.
- **Deploy gate** — lua artifacts deploy (`SCRIPT LOAD` → paced lines →
  `SCRIPT END` → `SCRIPT RUN`) only after an explicit `y`.
- `/samples /scan /conn /deploy /history /help /quit`, Ctrl+C cleanup,
  tee transcript per run, `--no-llm` dry-run mode.

## Decisions made

1. **Env vars win as a UNIT over `.llm_env`** (not per-variable as in
   llm_loop.py). During live verification the ambient shell key of a
   different provider (expired) mixed with the file's base URL and
   produced a 401 even though the file config was valid. Unit
   resolution prevents the class of failure; documented in the module
   docstring and pinned by unit tests.
2. **upload_script watches for the mid-upload rejection.** The F4.2
   bridge scans every data line fail-closed and answers a violating
   line immediately with `-612`, then resets the session. llm_loop.py's
   pattern (never read during the paced send) surfaces only the
   misleading `-611` state error from the `SCRIPT END` that follows.
   The assistant's upload now returns the real reason. (llm_loop.py
   shares this latent issue and is untouched per the spec; noted for a
   future fix.)
3. **Piped stdin is read ahead** (not polled) — scripted sessions are
   non-interactive by definition; reading the fixed script up front
   keeps the Prompt loop draining between turns.
4. **`/conn on [addr [type]]` maps to `CONN START`** — argless start =
   auto-connect with the preset target; `-451` (feature-off build)
   reports "conn not enabled on this firmware" (R2, unit-tested).
5. **Clean-up reports every stop outcome** (including the expected
   already-idle errors) so transcripts are evidence that cleanup ran.

## Problems encountered and resolutions

| Problem | Resolution |
|---------|------------|
| HTTP 401 on first live LLM turn | Ambient env key (foreign provider, expired) overrode the valid file key → unit-precedence fix (decision 1) |
| `-612` never surfaced on forbidden upload | Bridge rejects per-line and resets; send loop now watches for the mid-upload error (decision 2) |
| glm-5.2 answered instead of clarifying for "reduce the noise" | Not a bug — model preference; a prompt whose missing fact exists only in the user's head ("keep only my two personal devices") reliably yields `clarify` |
| glm-5.2 refused to emit a forbidden-token script | Expected consequence of the system prompt; the `-612` path was instead exercised through `upload_script` directly against live hardware (see evidence) |

## Test results

- **Unit** (`python tests/host/test_assistant.py`): 21/21 — envelope
  parsing (4 types, escaped-lua round-trip, fenced-json, fenced-lua
  fallback, malformed), buffer ingress/caps/snapshot dedup, history
  budget trim, context assembly (mode boundary present), unit-precedence
  config resolution, `upload_script` mid-upload `-612` (FakeSerial),
  `-451` classifier.
- **Live** (dongle on COM12, glm-5.2 via `.llm_env`), transcripts in
  `evidence-assistant-2026-08-28/`:

| §6 item | Evidence |
|---------|----------|
| 1 dry run, no LLM | `…190255.log` — banner, /help, /samples, prompt refused locally, /quit cleanup |
| 2 clarify round-trip | `…190818.log` — "llm asks: Which two addresses…?" then a filter built from the answer |
| 3 lua deploy, confirmed | `…191206.log` — y → SCRIPT RUN → filtered stream; exit shows `script_stop ok` |
| 3 n path + refusal | `…190900.log` — artifact kept on n; model refuses forbidden code (typed `error`) |
| 3 device `-612` gate | `minus-612-probe.txt` — live `upload_script` returns `sandbox violation: 'os.' is not allowed`; device stays alive |
| 4 mode boundary | `…191206.log` — conn-plane request answered "analysis-only", no lua offer |
| 5 feature-off `-451` | unit-tested (no feature-off build flashed this round); firmware literal verified in `cli_commands.c` |
| 6 cleanup | every transcript tail: SCRIPT/SCAN/CONN STOP outcomes + port close; Ctrl+C shares the same finally-path |
| 7 tee | the logs themselves |

## Lessons learned

- The NimBLE-era lesson repeats on the host side: read the actual
  device protocol at the source before coding against a summary. Two of
  the three defects found in verification (per-line bridge scan,
  per-command error envelope) were only visible in `cli_commands.c` /
  `lua_llm_bridge.c`.
- A strong system prompt makes a model a *co-enforcer* of the sandbox:
  it refuses to emit forbidden code before the device ever sees it. The
  device gate remains necessary (the model is not the trust boundary),
  but the layered behavior is worth keeping in the prompt.
- Per-variable config precedence is fine within one provider and wrong
  across providers; resolve configuration sources as coherent units.
