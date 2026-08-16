# Feature: Host LLM Loop Tool

## Basic Info

| Field        | Value                                        |
|--------------|----------------------------------------------|
| Feature ID   | H5.1                                         |
| Stage        | 5 — Host tooling                             |
| Layer        | Host (Python, PC side)                       |
| Dependencies | F4.1 (CLI), F4.2 (bridge), F3.2 (script mgmt) |
| Source Files | `llm_loop.py` (project root)                 |
| Test Files   | Field test on COM12 (dry-run + live LLM)     |

## Functional Description

Closes the product loop on the host PC: capture advertisement JSON from the
dongle, have an LLM generate a Lua filter/transform script for the captured
environment, deploy it over the F4.2 upload protocol, and verify the cleaned
output stream. The firmware is untouched; this feature is purely host-side.

```
  SCAN START ─▶ collect adv JSON ─▶ LLM generates Lua ─▶ SCRIPT LOAD/END
       ▲                                                       │
       └──────── clean JSON stream ◀─ on_adv/transform ◀─ SCRIPT RUN
```

Subcommands (single file, stdlib + pyserial only):

| Subcommand | Purpose                                                            |
|------------|--------------------------------------------------------------------|
| `capture`  | Open port, SCAN START, collect N s of adv JSON lines to a JSONL file |
| `analyze`  | Feed the capture to the LLM; write the generated Lua to a file for review |
| `deploy`   | Upload a Lua file via SCRIPT LOAD/END, SCRIPT RUN, verify the stream |
| `loop`     | capture → analyze → deploy → verify in one run                     |

### LLM backend

One OpenAI-compatible chat-completions client (urllib, no SDK):

| Env var       | Default                     | Meaning                    |
|---------------|-----------------------------|----------------------------|
| `LLM_BASE_URL`| `https://api.openai.com/v1` | Any OpenAI-compatible endpoint (DeepSeek, OpenRouter, Ollama) |
| `LLM_API_KEY` | (or `OPENAI_API_KEY`)       | Bearer token; Ollama accepts any value |
| `LLM_MODEL`   | `gpt-4o-mini`               | Model id                   |

`--dry-run` skips the LLM and deploys a bundled sample script so the whole
mechanical loop is testable with no key/network.

### System prompt contract (embedded)

Teaches the model the exact device ABI so generated scripts compile first try:

- `on_adv(addr, addr_type, rssi, name, uuids, manu_id, manu_data) -> bool`
  (true = pass, false = suppress); optional.
- `transform(addr, json_string) -> string` (returns the outgoing JSON line);
  optional.
- Incoming line schema: `{"addr","type","rssi","ts","name","uuids","manu"}`.
- Sandbox: only `string`, `table`, `math`, `utf8`; no `os`/`io`/`debug`,
  no `dofile`/`require` (bridge rejects with -612).
- Budgets: script <= 8 KB (bridge -803), hooks < 5 ms per report.
- Output contract: Lua source only, no markdown fences, no prose.

Sample input is capped (dedup by addr, <= 30 lines) to bound token cost.

## Acceptance Criteria

1. `capture` produces a JSONL file of valid adv lines; port stays open for the
   whole run (N3: closing resets the chip).
2. `analyze` with env-configured backend returns Lua that passes the bridge
   syntax validation on deploy; `--dry-run` writes the bundled sample.
3. `deploy` uploads via SCRIPT LOAD/END, runs SCRIPT RUN, and reports
   before/after line rates plus sample output lines.
4. `loop --dry-run` on real hardware completes capture → deploy → verify with
   zero manual steps and leaves the device in a clean state
   (SCRIPT STOP + SCAN STOP before exit).
5. No new Python dependencies beyond pyserial (already used by HIL suites).
6. Missing API key without `--dry-run` exits with a clear env-var hint.

## Test Cases

| ID   | Scenario                     | Expected                                          |
|------|------------------------------|---------------------------------------------------|
| TC-1 | `loop --dry-run` on COM12    | Full loop green; suppressed/transformed stream visible |
| TC-2 | `analyze` without key        | Clear error naming LLM_API_KEY / --dry-run        |
| TC-3 | `loop` with live LLM         | Generated script deploys (bridge ok) and stream changes per goal |
| TC-4 | `deploy` with bad Lua file   | Bridge compile error surfaced, no SCRIPT RUN      |

## Non-Functional Constraints

| Constraint | Requirement                                      |
|------------|--------------------------------------------------|
| Deps       | stdlib + pyserial only                           |
| Cost       | sample capped at 30 deduped lines per analyze    |
| Safety     | generated script runs in device sandbox; Ctrl+C restores default stream |
