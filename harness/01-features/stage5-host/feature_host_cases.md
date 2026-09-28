# Feature: Host Application-Case Framework (Plant Demo)

## Basic Info

| Field        | Value                                              |
|--------------|----------------------------------------------------|
| Feature ID   | H5.2                                               |
| Stage        | 5 — Host tooling                                   |
| Layer        | Host (Python + WinRT, PC side)                     |
| Dependencies | F2.4 (conn plane), F4.1 (CLI), H5.1 conventions (`.llm_env`) |
| Source Files | `host_app/run_case.py`                             |
| Test Files   | `tests/host/test_run_case.py` (19 unit tests); live transcripts in `harness/02-knowledge/evidence-host-cases-2026-08-28/` |
| Origin       | Proposal `docs/feature-proposal-host-cases-2026-08-28.md` + review `docs/review-h5-host-tooling-2026-08-28-deepseek.md` |
| Status       | Implemented and fully verified 2026-08-28, branch `feature/5-host-cases` — unit 24/24, hw 43/0 (2 informed SKIPs), demo 60/60 lines, estimate τ 9.5 vs 10.0 |

## Functional Description

A pluggable **application-case framework** on the host: a case
generates data, streams it over BLE GATT from a PC-side WinRT GATT
server, the dongle auto-connects by service UUID and re-streams it as
`"src":"conn"` JSON lines, which the runner collects into a JSONL
capture. With `--estimate`, the capture goes to the cloud LLM and its
answer is checked against the case's configured ground truth.

<!-- chart-id: CH-hostcases-md-01 rev1 -->
```
 PC plant (case)                          dongle (unchanged firmware)
 ┌──────────────────┐  GATT notify        ┌──────────────────────────┐
 │ y += (T/τ)(Ku−y) │ ──────────────────▶ │ ble_conn: subscribe,     │
 │ payload {"t","u","y"}                  │ json_encode_conn (merge) │
 └──────────────────┘                     └────────────┬─────────────┘
     ▲ CONN TARGET/START (auto) ───────────────────────┘  USB CDC
     └── "src":"conn" lines ──▶ capture JSONL ──▶ LLM (--estimate, checked)
```

### The case contract (pluggable pattern)

```python
class Case:
    name, period_s, read_only, svc_uuid, chr_uuid, llm_task
    add_args(parser)      init(args)          step(t) -> bytes
    read_value(t) -> bytes (read_only path)   ground_truth() -> str
    check(samples) -> [(line, ok)]            state() -> str
CASES = {"first_order": FirstOrderCase}       # append-only registry
```

A new case = one class + one registry line; the runner lifecycle never
changes. Ships `first_order` (notify path, Euler plant
`y += (dt/τ)(K·u − y)`, u steps 0→1 at `--step-at`) and
`first_order_poll` (2026-08-29): identical plant and ground truth
delivered through the dongle's POLL path — the peer exposes a
read-only characteristic so the dongle cannot subscribe and polls on
its CONN INTERVAL; verified live 30/30 lines, mode `poll`, physics
green.

### Runner lifecycle

preflight stop **+ state settle to `off`** → peer up (full diagnostics
on failure) → `CONN TARGET` **before** any scan → optional `--with-scan`
→ `CONN START` → poll `CONN STATUS` to `active` (≤15 s) →
`wait_subscribed` → single-threaded tick loop (0.2 s drain slices;
notify on cadence; payload hard-capped at 253 bytes; JSONL capture;
status line every 10 s) → `case.check()` → optional LLM act → cleanup.

### Outcome policy

| Outcome | Treatment |
|---------|-----------|
| Data path available | scored: ≥90 % line ratio + envelope/payload checks + physics checkpoints (exit 0/1) |
| PC cannot serve the GATT-server role | full exception + remediation hints, exit 3 |
| Session never becomes active | pass-with-artifact (precise status + transcript), exit 0 |

### LLM act (`--estimate`)

All captured lines (≤ `LLM_MAX_SAMPLE_LINES = 60`) + the case's
`llm_task`; reply parsed defensively (plain or fenced JSON); τ_est
within ±30 % of the configured τ; missing key → skip with a message.
Config resolution: env vars win as a unit; the `.llm_env` file may
carry the legacy `LLM_*` triple or provider pairs (`DASHSCOPE_*`, then
`TOKEN_PLAN_*`; model `LLM_MODEL` or `QWEN_MODEL`).

## Acceptance Criteria

1. `--help` works with no hardware; case-specific flags visible.
2. Full run on COM12: ≥90 % of expected conn lines; every line has the
   `ts`/`addr`/`src:"conn"` envelope with `t`/`u`/`y` merged; physics
   checkpoint y(step_at+τ) within 10 % of 63.2 %·K (Euler included);
   steady state within 10 % of K.
3. `--estimate` produces parseable estimates with τ within ±30 %; no
   key skips the act cleanly.
4. Peer-start failure prints the precise exception + remediation hints
   and exits 3; session-never-active is a pass-with-artifact.
5. Ctrl+C and normal exit run cleanup (`CONN STOP` [+ `SCAN STOP`]),
   port kept open until orderly exit (N3).
6. Self-contained: no imports from `test_ble_conn_hw.py`, `llm_loop.py`
   or H5.3 files (disjoint branches, merge order irrelevant).

## Test Cases

| ID   | Scenario | Expected / result |
|------|----------|-------------------|
| TC-1 | `--help` | works offline; case flags shown ✓ |
| TC-2 | unit suite | 24/24 ✓ |
| TC-3 | runner sequence (raw-line driver) | TARGET → auto START → active → notify → merged `src:"conn"` line ✓ |
| TC-4 | demo attempt 1 | blocked by firmware BUG A (malformed TARGET response) — fixed, both forms wire-verified ✓ |
| TC-5 | full §6.2 demo + `--estimate` | 60/60 lines, all checks green; τ_est 9.5 vs 10.0 (5 % error, qwen3.8-flash) ✓ |

## Non-Functional Constraints

| Constraint  | Requirement |
|-------------|-------------|
| Deps        | stdlib + pyserial + winsdk (WinRT); single file, single-threaded |
| Payload     | ≤ ~40 B design, 253 B hard cap (`NOTIFY_CAP`) |
| Safety      | firmware untouched by the runner; ground truth checked locally |
| Evidence    | every run tee'd; transcripts in the evidence dir |

## Fixed on the way (F2.4 line, this branch)

- **BUG A**: two-UUID `CONN TARGET` ok-response was invalid JSON
  (missing `chr` closing quote) — `cli_commands.c` now emits two
  explicit valid forms; C1 asserts the `svc`/`chr` echo.
- **BUG B**: `CONN STOP` ok can precede the state flip to `off` —
  state-settle waits added (runner + suite); firmware-side fix left as
  an F2.4 backlog note.
- **BUG B'**: hw-suite response reader now matches the `cmd` field to
  the issued command (stale-line false passes excluded); C2's
  START-rejected quirk shape is an informed SKIP.

## Future Extensions

- ~~`read_only` case~~ — SHIPPED 2026-08-29 as `first_order_poll`
  (live: 30/30 polled lines, mode `poll`, physics green).
- `--peer external` axis for real sensors (per-case UUIDs already in
  the contract).
- Convergence refactor with H5.1/H5.3 shared helpers (canonical copy:
  `llm_loop.py`) once both stage-5 features have landed.
