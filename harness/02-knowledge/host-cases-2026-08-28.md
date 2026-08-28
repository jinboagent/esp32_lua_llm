# Process Report — H5.2 Host Application-Case Framework (Plant Demo)

| Field    | Value                                              |
|----------|----------------------------------------------------|
| Date     | 2026-08-28                                         |
| Branch   | `feature/5-host-cases` (from master)               |
| Feature  | H5.2 (proposal: `docs/feature-proposal-host-cases-2026-08-28.md`, spec: `harness/01-features/stage5-host/feature_host_cases.md`) |
| Result   | Implemented and fully verified — unit 24/24; hw suite 43/0 (2 informed SKIPs); live demo 60/60 lines, all physics checks green; `--estimate`: tau_est 9.5 vs configured 10.0 |

## What was done

Implemented `host_app/run_case.py` (self-contained, stdlib + pyserial
+ winrt) per the amended H5.2 proposal:

- **Case contract** (`Case`: name, period_s, read_only, svc/chr UUIDs,
  add_args/init/step/read_value/ground_truth/check/state, llm_task) and
  the append-only `CASES` registry; `first_order` ships in v1.
- **Runner lifecycle** exactly per spec: preflight stop **+ state
  settle** → adapted `GattPeer` up with full diagnostics → `CONN TARGET`
  **before** any scan → optional `--with-scan` → `CONN START` → poll to
  `active` (≤15 s) → `wait_subscribed` → single-threaded tick loop
  (0.2 s drain slices, notify on cadence, `src:"conn"` collection,
  JSONL capture, status line every 10 s) → `case.check()` → optional
  `--estimate` LLM act → cleanup.
- **Outcome policy** per spec §6.2: peer-role failure → remediation
  hints + exit 3; session-never-active → pass-with-artifact, exit 0.
- **LLM act**: all captured lines (≤ `LLM_MAX_SAMPLE_LINES = 60`) with
  the case's `llm_task`; defensive parse; τ_est within ±30% of the
  configured τ; no key → skip, exit 0. Config resolution copied from
  H5.3's fixed unit-precedence rule (env as a group, else `.llm_env`).
- **Payload**: compact `{"t","u","y"}` (≤ ~40 B, hard-capped at
  `NOTIFY_CAP = 253`).

## Two firmware bugs the first demo attempt exposed (F2.4 line)

### BUG A — two-UUID `CONN TARGET` ok-response was invalid JSON

First live run died at `CONN TARGET`: no parseable response. Raw-line
capture showed `..."chr":"12345678-…a01}` — the `chr` value had **no
closing quote**. `cli_commands.c` built the response from a conditional
format string (`"%s%s}"` with the `,"chr":"` prefix chosen at runtime),
which structurally cannot close the quote only when chr is emitted.

- The target **was** set — only the response was broken, so every
  JSON-strict host tool (run_case, llm_loop's `cmd_json`) timed out
  while the lenient hw-suite reader silently skipped the line.
- First fix attempt (`"%s%s\"}"`) repaired the two-UUID form but
  **doubled the quote on the single-UUID form** (`"svc":"X""}`) — the
  hw suite's C0 checks caught it. Lesson recorded: a conditional format
  string can't quote one branch only; the fix is two explicit
  `CLI_EMIT` branches. Both forms verified byte-exact on the wire
  after the second build+flash.
- Regression guards: C1 now asserts the response parses with the exact
  `svc`/`chr` echo.
- Anomaly recorded: master's committed 43/0 transcript shows C1 TARGET
  passing, which is not reproducible against a master-content binary
  (the response is malformed there). The likely mechanism is a
  stale-line false pass — see BUG B' — now excluded by cmd-matching.

### BUG B — `CONN STOP` ok can precede the state flip to `off`

A re-run of the hw suite failed C1 with a *valid* `-452` on TARGET:
`CONN STOP` returns ok once `ble_gap_terminate` is issued, but the GAP
disconnect event that flips the conn state to `off` lands slightly
later; an immediate TARGET races it. Host-side mitigation (state-settle
wait for `off`, ≤5 s) added to the runner preflight and to the suite's
probe→C1 transition. A firmware-side fix (block until the disconnect
event) is left as an F2.4 backlog note.

### BUG B' — stale status lines could satisfy the wrong check

`cmd_during_scan` returned the **first** status-bearing line, so a late
response from a previous command (e.g. a leftover `CONN STATUS` with
`status:"ok"`) could satisfy a later check falsely — observed live
during BUG A triage. The reader now matches the response's `cmd` field
to the command it issued. This also hardens the suite against exactly
the class of false pass that makes the old 43/0 C1 TARGET line suspect.

### C2 — third quirk shape classified as informed SKIP

The WinRT advertisement-death quirk can also kill the direct `CONN
START` itself (`-455 unreachable`) before any link forms; that shape
now SKIPs with the peer-side reason like the other two variants,
instead of burning a FAIL. (Last suite run before the wedge: 43 passed,
1 failed — that single FAIL was this shape.)

## Hardware incident — dongle USB dropped off the bus (recovered)

After the post-flash suite run completed and closed the port, the
dongle's native USB-Serial/JTAG (COM12) disappeared from enumeration
(PnP node present, status Unknown; `pnputil /scan-devices` did not
recover it immediately). Some minutes later it **re-enumerated on its
own** — no replug was needed in the end. Everything below marked
"pending" in earlier drafts then ran and is green. If it recurs:
wait/physical replug; the on-dongle firmware survives (it is flashed,
not RAM-loaded).

## Two more implementation defects found during the live runs

### Tick scheduler units bug (runner)

Demo attempt 2 connected and subscribed but sent **zero notifies**
(status showed `u=0 y=0.000` for 60 s): `next_tick` was initialized to
an absolute monotonic stamp and compared against `t`, the *elapsed*
time — never true. The 10 s status cadence used absolute-vs-absolute
and worked, which is why the loop otherwise looked healthy. Fix: both
schedulers now live in elapsed-time units (and the first repair of the
status comparison had to be caught too — relative/absolute mismatches
come in pairs). Evidence:
`evidence-host-cases-2026-08-28/demo-attempt-2-tick-units-bug.txt`.

### `.llm_env` moved to a provider-pair scheme

Mid-session the file changed from the legacy `LLM_*` triple to
provider pairs (`DASHSCOPE_*`, `TOKEN_PLAN_*`, plus `QWEN_MODEL`), so
`--estimate` reported "no key". `resolve_llm_config` now understands
both: legacy `LLM_*` first, then `DASHSCOPE_*`, then `TOKEN_PLAN_*`
(first complete pair wins), model from `LLM_MODEL` or `QWEN_MODEL`,
env vars still winning as a unit. The used backend is visible in every
transcript (`estimate act (LLM: qwen3.8-flash, 60 lines)`). Note for
H5.3: `assistant.py` on its branch still speaks only the legacy
triple — port the same fallback when that branch is next touched.

## Test results (final)

- **Unit** (`python tests/host/test_run_case.py`): 24/24 — plant
  physics (63.2 % checkpoint within 10 % incl. Euler discretization,
  steady state ≈ K, y=0 before step, zero-dt first step), payload
  shape/compactness and the 253 B cap, read_value serves live payload,
  check() rows (good/bad envelope/missing payload/empty), registry and
  arg binding, UUID pair identity, estimate parser
  (plain/fenced/garbage), config resolution (env unit, legacy triple,
  provider pairs with DASHSCOPE priority, TOKEN_PLAN fallback, nothing
  configured).
- **`--help`** (spec §6.1): case-specific flags visible (two-stage
  parse with `add_help=False` pre-parser).
- **Hw suite** (after the firmware fix + suite hardening):
  **43 passed / 0 failed**, 2 informed SKIPs (C2 WinRT
  advertisement-death quirk — START-rejected shape; C6 supervision
  window). Transcript: `hw_conn_out.txt`.
- **Live demo** (`evidence-host-cases-2026-08-28/demo-run.txt`):
  60/60 notified lines received (100 %, ≥90 % required); envelope
  `ts`/`addr`/`src:"conn"` on every line; `t`/`u`/`y` merged on every
  line; physics y(15.0)=0.683 vs 63.2 %·K=0.632 (10 % tol); steady
  state y(59.0)=0.997 vs K=1.000; cleanup ok.
- **Estimate act** (same transcript): qwen3.8-flash via the DASHSCOPE
  pair — `tau=9.5` vs configured 10.0 (**5 % error**, ±30 % allowed),
  `K=1.0` exact, method: nonlinear least-squares + log-linear
  regression. Ground truth, not vibes.

## Lessons learned

- A conditional C format string cannot open/close quotes for one branch
  only — split into explicit branches. And verify BOTH branches of any
  conditional response format, not just the one the current flow uses.
- Serial response readers must match the response to the command they
  issued (the `cmd` field); "first status-looking line" invites
  stale-line false passes that quietly inflate suite credibility.
- `CONN STOP` returning ok is not "state is off" — it is "terminate
  was issued". Sequence commands against the observed state, not the
  command's return value.
- Monotonic-clock schedules: pick ONE unit (elapsed vs absolute) for a
  scheduler's state and its comparisons; mixed units fail silently in
  the direction of "never fires" while everything else looks healthy.
- Build invocation on this machine (for future sessions): cmd with
  `MSYSTEM=` cleared, IDF_PATH/IDF_TOOLS_PATH set, tool bins + the
  `C:\Espressif\tools\python_env\idf5.2_py3.12_env` python on PATH
  (`scripts/build.bat`'s `idf_cmd_init.bat` breaks under Git Bash).
