# Feature Proposal — Interactive LLM Assistant Session (Message Prompt)

| Field       | Value                                              |
|-------------|----------------------------------------------------|
| Date        | 2026-08-28                                         |
| Branch      | `feature/5-assistant` (at implementation)          |
| Status      | **PROPOSAL — amended 2026-08-28** (review R1, R2, Rec1–Rec3 from docs/review-h5-host-tooling-2026-08-28-deepseek.md incorporated) — awaiting final sign-off |
| Author      | ZCode side-session design discussion with product owner (2026-08-28) |
| Future ID   | **H5.3** (host-tooling stage 5; H5.1 = llm_loop.py, H5.2 = host cases) |
| Scope       | Review document only; no code changes yet          |

**What reviewers are asked to evaluate:** (1) the feature requirement,
especially the **message Prompt** and the **typed response contract**
(§2.2–§2.5 — both are mandatory, per product-owner instruction), (2) the
interaction model decisions (§2.2), (3) independence from H5.2 (§4),
(4) the verification plan (§6). After approval this document is promoted
to a harness feature spec and implemented in a follow-up AI session (§8).

---

## 1. Background & Motivation

`llm_loop.py` (H5.1) already composes user text with device data — but
statically: `--goal` is set once at launch, the prompt is
advertisement-oriented, the output is assumed to be Lua, and there is no
conversation. The product vision is **human-in-the-loop**: an operator on
the PC steers the cloud LLM with their own prompts while the dongle keeps
streaming JSON; the LLM answers, requests clarification, or produces a Lua
artifact that is deployed only on explicit human confirmation. This is
mixed-initiative interaction — either party (user *or* LLM) may steer —
and it completes the product loop the firmware was built for (espclaw-style
agent access, but with a person holding the wheel).

## 2. Feature Requirement

### 2.1 User stories

- As an IoT developer, I want to ask questions about the live device data
  ("which of these are Apple devices?") and get answers, not just scripts.
- As an IoT developer, I want the LLM to ask ME for more information when
  my request is ambiguous, instead of guessing.
- As an operator, I want Lua artifacts deployed only after I confirm.
- As a product owner, I want one session tool that works on both data
  planes: advertisement lines (where Lua can act) and connection lines
  (analysis-only).

### 2.2 Product-owner decisions (confirmed 2026-08-28, this session)

| Decision | Choice |
|----------|--------|
| Interaction model | **Interactive session** (REPL), not one-shot flags |
| Memory | **Multi-turn history** within a session (trimming policy = open question) |
| Placement | `host_app/assistant.py`, **self-contained** (copied helpers, house style; no imports from `llm_loop.py` or H5.2 files) |
| Response protocol | **Typed JSON envelope** (§2.4) — mandatory |
| Device-data attachment | **Snapshot-only**: every turn carries the current buffer view; the LLM cannot pull data on demand in v1 (recorded as future extension) |
| Deployment | **Only on explicit user confirmation** (`deploy? [y/N]`) |
| `llm_loop.py` | Untouched |

Location note (review Rec2): the repo root is the legacy H5.1 home —
`llm_loop.py` stays put; `host_app/` is the new home for host tooling;
`llm_loop.py` relocates only on an explicit later decision. Do not "fix"
the split by moving files.

### 2.3 The message Prompt (named component — REQUIRED, not optional)

The heart of the feature is a **message Prompt** (classic event-loop term:
a loop that receives messages arriving from multiple sources and
dispatches them), multiplexing two inputs onto one processing loop:

```
ESP32 ──USB JSON lines──┐
                        ▼                    ┌────────────────┐
user ──console input──▶ MESSAGE Prompt ──────▶ │ context        │──▶ cloud LLM
                        · ingress validation │ assembly:      │
                        · bounded buffers    │ history +      │
                        · routes typed       │ device snapshot│
                          replies back       └────────────────┘
```

Prompt responsibilities (all AC-level requirements):

1. **Continuous drain** — a reader that drains the serial port while the
   user thinks, so the device is never left blocked and the buffer stays
   fresh (keep-port-open, N3). Mechanism (review R1): **non-blocking
   stdin** — `msvcrt.kbhit()` + `getwch()` on Windows,
   `select([sys.stdin], …)` on POSIX — polled between short serial
   timeouts. A blocking `input()` is explicitly rejected: it stalls the
   drain for exactly as long as the user thinks, silently violating this
   requirement.
2. **Ingress validation** — only lines that parse as JSON enter the device
   buffer; invalid lines are dropped and counted (never crash on garbage;
   command responses and notices are classified by their fields).
3. **Bounded rolling buffer** — recent adv AND conn lines (classified by
   `src`), capped **per plane** (review answer: sample adv, keep all
   recent conn).
4. **Conversation history** — bounded, trimmed by a **token-budget cap**
   (review answer: matches the model's real context limit; turn counts do
   not).
5. **Context assembly** — on each user turn, build ONE request: system
   prompt (mode boundary, §2.6) + trimmed history + a reduced view of the
   device buffer + the user's prompt. This is the "put multiple JSON
   history together, validated" step.
6. **Typed-reply routing** — dispatch the LLM's envelope (§2.4).

### 2.4 Typed JSON response contract (mandatory)

The system prompt must require the LLM to reply with **JSON only**, in
this envelope (defensively parsed; parse failure → one retry with a
corrective instruction, then a clear error to the user — never a crash):

```
{"type": "answer",  "text": "..."}                          plain reply over the data
{"type": "lua",     "text": "why", "code": "-- Lua…"}       artifact → host offers deploy [y/N]
{"type": "clarify", "text": "question for the user"}        mixed initiative: loop continues
{"type": "error",   "text": "..."}                          e.g. "insufficient data"
```

The **clarify** type is the product owner's explicit requirement: the LLM
must be able to ask the user for more information, and the session loops
until the user is satisfied. The **lua** type routes to the existing
SCRIPT LOAD/END/RUN protocol — the device's fail-closed sandbox scan and
compile-check remain the final gate, and the human confirmation is the
gate before that.

`lua.code` is the highest-risk field in the envelope (review Rec1): a
script full of quotes, newlines and backslashes inside a JSON string is
the single most likely place an LLM emits invalid JSON. The one allowed
retry must use an **escaping-focused corrective instruction**, and when
strict parsing still fails, the fallback is extracting a fenced
` ```lua ` code block from the raw reply before erroring to the user.

### 2.5 Session surface (usage grammar)

```
python host_app/assistant.py [port]

you: <free text — tasking for the LLM>
llm: (answer | lua artifact + deploy? [y/N] | clarify question | error)
/samples [n]     show the current device buffer snapshot
/scan on|off     control the adv plane
/conn on|off     control the connection plane (CONN START/STOP)
/deploy          re-offer the last lua artifact for deployment
/history         show conversation summary
/help, /quit     …
Ctrl+C           universal stop + cleanup (N3 keep-open honored)
```

`/conn on|off` must degrade gracefully on feature-off firmware (review
R2): a build with `CONFIG_BLE_CONN_ENABLED=n` answers `CONN START/STOP`
with `-451 "not compiled in"` — the assistant reports "conn not enabled
on this firmware" and the session continues; it never mis-reports
success or hangs waiting for conn lines that cannot arrive.

### 2.6 Mode boundary (system-prompt requirement)

The system prompt must state, and the assistant must display, which mode
applies: **advertisement data → the LLM may emit Lua artifacts** (hooks
`on_adv`/`transform` act on that plane); **connection data (`src:"conn"`)
→ analysis-only** (conn lines deliberately bypass the Lua hooks, F2.4
design). The LLM must not offer Lua for conn-plane requests.

## 3. As-Is Seam Analysis

**Reused, exists today:**
- Serial conventions: `cmd_json`, line collection by field signature
  (`"addr"` presence, `"src":"conn"` discriminator), N3 keep-open, Ctrl+C
  handling (llm_loop.py; test_ble_conn_hw.py patterns).
- LLM helper conventions: `.llm_env` resolution, `LLM_BASE_URL` /
  `LLM_API_KEY` | `OPENAI_API_KEY` / `LLM_MODEL` (default `gpt-4o-mini`),
  urllib POST, temperature 0.2, timeout 120 — copied, per house style.
- Deployment path: `SCRIPT LOAD` → paced lines → `SCRIPT END` →
  `SCRIPT RUN` (llm_loop.py `upload_script` pattern).
- Safety chain: device-side sandbox token scan + compile-check → `-612`
  on violation; nothing new needed firmware-side.

**Gaps (all host-side):**
1. No REPL exists — a console loop with **non-blocking stdin** (review
   R1: `msvcrt.kbhit()` + `getwch()` on Windows, `select()` on POSIX) so
   the Prompt keeps draining the serial port while the user types;
   single process, no threads for v1. A blocking `input()` loop is the
   *rejected* design — it silently violates the continuous-drain
   requirement (§2.3, item 1).
2. No typed-reply contract anywhere — new system prompt + defensive
   envelope parser.
3. No conversation history management — new, bounded.

## 4. Design Options & Trade-offs

### Option A — Self-contained `host_app/assistant.py` (RECOMMENDED)
One new file; helpers copied; no imports from `llm_loop.py` or H5.2's
files. True parallel-branch independence with H5.2 — disjoint files, zero
merge conflicts, merge order irrelevant. Cost: ~80 lines of duplicated
serial/LLM helpers (the repo already accepts this cost across 13 scripts).

### Option B — `chat` subcommand inside `llm_loop.py`
One entrypoint, but grows a 380-line script with new concerns (REPL,
Prompt, history) and makes the two features share a file — conflicts with
the parallel-AI-branch requirement. Rejected.

### Option C — Shared `host_app/common.py` with H5.2
DRY, but couples both branches onto one file. Rejected for v1; a
convergence refactor may follow after both features land. For that
refactor (review Rec3): `llm_loop.py`'s helpers are the **canonical
copy** the shared module is derived from.

## 5. Impact Analysis

| File | Kind | Change | Risk |
|------|------|--------|------|
| `host_app/assistant.py` | new | message Prompt + REPL + typed contract + history | low (host-only, new file) |
| `docs/` usage notes | additive | one section | low |

**Zero firmware changes. Zero edits to existing scripts.** Disjoint from
H5.2's files by construction.

## 6. Test & Verification Plan

1. `python host_app/assistant.py --help` / no-key dry-run: session starts,
   `/help` `/samples` `/quit` work without an API key (LLM act reports
   "no key" cleanly).
2. Scripted session transcript (piped stdin): a question against a
   captured buffer yields a typed envelope; a deliberately ambiguous
   prompt yields a `clarify` round-trip; the follow-up answer completes
   the exchange (this is the mixed-initiance AC).
3. Lua path: an `answer→lua` turn offers deploy; `y` runs SCRIPT LOAD/
   END/RUN; the device's `-612` path is exercised by requesting a script
   containing a forbidden token (e.g. `os.`) — expected: device rejects,
   assistant reports it, session continues.
4. Mode boundary: a conn-plane request must not produce a `lua` offer.
5. Feature-off firmware (review R2): against a `CONFIG_BLE_CONN_ENABLED=n`
   build, `/conn on` reports "conn not enabled on this firmware" and the
   session continues (no hang, no false success).
6. Ctrl+C and `/quit` → cleanup (SCRIPT STOP + SCAN STOP + CONN STOP),
   port closed only at orderly exit (N3).
7. Transcript saved via tee — becomes the review evidence.

## 7. Open Questions — RESOLVED by review (2026-08-28)

| # | Question | Resolution |
|---|----------|------------|
| 1 | History trimming policy | **Token-budget cap** — matches the model's real context limit; turn counts do not |
| 2 | Device buffer sizing | **Per-plane caps** — sample adv lines, keep all recent conn lines |
| 3 | Session persistence | **Yes** — tee transcript per run (§6.7) |
| 4 | `request_data` type (LLM pulls data on demand) | Deferred to **v2** |
| 5 | `/deploy <file.lua>` | Nice-to-have, **low priority** — not in v1 |

## 8. Post-Review Path

1. Reviewer sign-off (or amendments) on this document.
2. Promote to `harness/01-features/stage5-host/feature_assistant_session.md`
   as **H5.3** (H5.1 template; Origin = this proposal + review doc).
3. Implement on `feature/5-assistant` per the spec → verification §6 →
   status report + `LATEST.md` pointer → squash-merge (keep branch).
4. Explicitly parallel-safe with H5.2 (disjoint files); merge order
   irrelevant.

## 9. AI Implementation Guidance (for the generating session)

- **Read first:** `CLAUDE.md` → `harness/00-global-context/` →
  `harness/01-features/stage5-host/feature_llm_loop_tool.md` (H5.1) →
  this proposal → `llm_loop.py` (serial + LLM + upload_script patterns) →
  `docs/feature-proposal-host-cases-2026-08-28.md` (H5.2 — for the shared
  vocabulary of context assembly, NOT for imports).
- **Branch:** `feature/5-assistant`; never commit to master directly.
- **Commit format:** the 4-section standard (What/Why/How/Verification).
- **Hard rules:** the message Prompt and the typed JSON envelope are
  mandatory features, not nice-to-haves; stdin is non-blocking — never
  `input()` (R1); handle `-451` on `/conn` as "not enabled on this
  firmware" (R2); the `lua.code` retry is escaping-focused with a fenced
  ``` ```lua ``` fallback (Rec1); self-contained file (no imports from
  llm_loop.py or H5.2 files); deploy only after explicit `y`; conn data
  is analysis-only (mode boundary in the system prompt); Ctrl+C cleanup
  on every exit path; port stays open until orderly exit (N3).
- **Done means:** §6 verified end-to-end with a saved transcript,
  including at least one `clarify` round-trip and one rejected-forbidden-
  token deploy attempt.
