# Feature Proposal — Host Application Case Framework (Plant Demo)

| Field       | Value                                              |
|-------------|----------------------------------------------------|
| Date        | 2026-08-28                                         |
| Branch      | `feature/5-host-cases` (at implementation)         |
| Status      | **IMPLEMENTED 2026-08-28** — promoted to `harness/01-features/stage5-host/feature_host_cases.md` (H5.2) on branch `feature/5-host-cases`; verification report: `harness/02-knowledge/host-cases-2026-08-28.md` (implementation exposed two F2.4 firmware bugs, fixed on the same branch; final live demo pending a dongle replug) |
| Author      | ZCode side-session design discussion with product owner (2026-08-28) |
| Future ID   | **H5.2** (host-tooling stage 5; H5.1 = llm_loop.py) |
| Scope       | Review document only; no code changes yet (the implementation lives on the branch above) |

**What reviewers are asked to evaluate:** (1) the feature requirement
itself (§2), (2) the pluggable design pattern and its independence from
H5.3 (§3–§4), (3) the impact on the shipped firmware (§5 — none), (4) the
verification plan (§6). After approval this document is promoted to a
harness feature spec and implemented in a follow-up AI session (§8).

---

## 1. Background & Motivation

The dongle's connection plane (F2.4, `ble_conn`) is now hardware-verified
**end-to-end**. The GATT data path (C1–C6) had never run before
2026-08-28: a test-side WinRT bug (synchronous calls, fixed by the
asyncio/await port) had been masking two firmware defects — the discovery
callbacks treated NimBLE's per-record `error->status == 0` calls as
errors and killed the link on the first arriving service, and
`json_encode_conn`'s merge omitted the separators between envelope and
payload members (invalid JSON). Both are fixed and verified
(`docs/fix-gatt-data-path-2026-08-28.md`): the hardware suite now shows
43 passed / 0 failed, with C1 proving connect → discover → subscribe →
notify re-stream of valid merged `"src":"conn"` JSON.

**State for this feature (2026-08-28):**

- **The PC peer works.** The full demo path this proposal builds on is
  proven on this machine — §6.2a (full success) is the expected first-run
  outcome here, not the diagnostic path.
- One WinRT peer quirk remains: the PC's connectable advertisement does
  not reliably survive a connection cycle, making *direct-address*
  reconnects flaky against the PC peer (hw C2 SKIPs with the reason; the
  test peer logs advertisement-status transitions). The demo's
  auto-connect-by-UUID flow is unaffected. `--peer external` stays a
  designed-but-deferred axis for real sensors, not a workaround.
- Runner rule kept: set `CONN TARGET` **before** `SCAN START` (the
  two-UUID TARGET form gets no response during an active scan — observed
  2026-08-28; the target is still set, but set it while scanning is off).

This proposal kills two birds:

1. **A demo application case** — the "digital-twin sensor": the PC
   simulates a first-order inertial plant, streams it over BLE GATT, the
   dongle auto-connects and re-streams it as `"src":"conn"` JSON lines, and
   the host (optionally) asks the cloud LLM to estimate the plant's time
   constant — with checkable ground truth.
2. **A portable diagnostic** — the peer preflight prints and records the
   exact failure on any machine (other machines may lack the GATT-server
   role or hit the documented package-identity restriction, and the
   preflight settles that locally).

It also establishes the **host application-case framework**: a pluggable
registry of data-generator "cases" so future application cases (different
plant models, payload shapes, transport behaviors) plug in without touching
the runner — mirroring the append-only extension principle the firmware
side already follows (CLI families, error ranges, Kconfig flags).

## 2. Feature Requirement

### 2.1 User stories

- As a product owner, I want to demo the dongle reading live "sensor" data
  from the PC over BLE, so the connection plane can be shown end-to-end.
- As an engineer, I want the demo to have ground truth, so the LLM's answer
  is verifiable (not vibes).
- As a future case author, I want to add a new application case by writing
  one class and one registry line, so parallel AI sessions never conflict.

### 2.2 Product-owner decisions (confirmed 2026-08-28, this session)

| Decision      | Choice |
|---------------|--------|
| Peer in v1    | **PC only** (WinRT GattPeer); `--peer external` axis designed, deferred |
| Entrypoint    | `host_app/run_case.py` (single command, `--case` selects) |
| LLM act       | Included in v1 (`--estimate` flag) |
| Concurrency   | Single-threaded main loop (matches test-suite blocking style; avoids WinRT threading risk) |
| Independence  | **Self-contained file(s); no shared module with H5.3** — copied helpers per repo house style (13 standalone scripts, zero cross-imports) |

### 2.3 Proposed defaults (reviewable)

- Plant: `y += (T/τ)·(K·u − y)` (Euler), `u` steps 0→1 at `t = step_at`.
- `--tau 10.0`, `--k 1.0`, `--step-at 5.0`, `--secs 60.0`, `--interval 1.0`
  (matches the dongle's default poll interval), `--port COM12`,
  `--out plant_capture.jsonl`, `--estimate`, `--with-scan` (optional adv
  plane alongside; counted separately).
- Payload (≤ ~40 bytes, far under the 253-byte notify cap):
  `{"t": <plant time>, "u": <input>, "y": <output>}` — a JSON object, so
  the dongle **merges** its fields into the envelope
  (`{"ts":…, "addr":…, "src":"conn", "t":…, "u":…, "y":…}`).
- Ground truth: y(t_step + τ) ≈ 63.2%·K; steady state ≈ K·u.
- UUIDs: reuse the test's documented pair (`SVC_UUID`/`CHAR_UUID` from
  `test_ble_conn_hw.py`) as constants.

### 2.4 Usage grammar

```
python host_app/run_case.py [port] --case first_order \
    [--tau 10] [--k 1] [--step-at 5] [--secs 60] [--interval 1] \
    [--estimate] [--with-scan] [--out plant_capture.jsonl]
```

Output: live status line every 10 s (t, u, y, lines received); summary at
end (received vs expected lines, envelope/payload checks, physics
checkpoints); with `--estimate`, a configured-vs-estimated τ/K table.

### 2.5 Data flow

```
 PC plant thread-in-main-loop                dongle (unchanged firmware)
 ┌──────────────┐  GATT notify 1 Hz   ┌──────────────────────────────┐
 │ y += (T/τ)(Ku−y)│ ───────────────▶ │ ble_conn: subscribe,         │
 │ payload {"t","u","y"}│              │ json_encode_conn (merge)    │
 └──────────────┘                      └──────────────┬───────────────┘
     ▲ CONN TARGET/START (auto) ──────────────────────┘  USB CDC
     └── "src":"conn" lines ── collect ──▶ plant_capture.jsonl ──▶ LLM (optional)
```

The firmware adv path is byte-for-byte untouched.

### 2.6 The case contract (the pluggable pattern)

```python
class Case:
    name: str                    # "first_order"
    period_s: float              # notify cadence
    read_only: bool = False      # True → exercises the dongle poll path
    def add_args(self, parser): ...          # case-specific CLI flags
    def init(self, args): ...                # bind parameters
    def step(self, t: float) -> bytes: ...   # next payload to notify
    def ground_truth(self) -> str: ...       # what the data SHOULD show
    def check(self, samples) -> list[str]:   # pass/fail lines for the report
    llm_task: str                # prompt fragment for the --estimate act

CASES = {"first_order": FirstOrderCase}      # append-only registry
```

Runner lifecycle (fixed skeleton, calls only the contract): preflight stop
→ peer up (with diagnostic) → `CONN TARGET` (**before** `SCAN START`: the
two-UUID TARGET form gets no response during an active scan — observed
2026-08-28; the target is still set, but set it while scanning is off) →
optional `SCAN START` → `CONN START` → poll `CONN STATUS` ≤ 15 s to
`active` → `wait_subscribed` → single-threaded tick loop (drain serial in
~0.2 s slices; `case.step()` on each `interval` boundary; collect
`"src":"conn"` lines; append JSONL) → `case.check()` → optional LLM act →
cleanup (`CONN STOP` [+ `SCAN STOP`]).

## 3. As-Is Seam Analysis

**Reused, exists today:**
- Serial conventions: `cmd_json` (llm_loop.py:88), keep-port-open (N3),
  Ctrl+C universal stop.
- CONN orchestration + the `"src":"conn"` collection pattern
  (test_ble_conn_hw.py C1 flow, :247-295).
- `.llm_env` LLM env resolution (`LLM_BASE_URL`, `LLM_API_KEY` |
  `OPENAI_API_KEY`, `LLM_MODEL` default `gpt-4o-mini`; urllib POST,
  timeout 120, temperature 0.2).
- `GattPeer` (tests/hw/test_ble_conn_hw.py:81-222 — asyncio/await rewrite
  2026-08-28; binding rules documented in vendor_reference/ble_test/
  DESIGN.md): service/char creation, connectable advertising,
  `notify(bytes)`, `wait_subscribed()`, `stop()`, plus the session-probe
  skip gate (:339-373) the runner should mirror.

**Gaps (all host-side, contained):**
1. `test_ble_conn_hw.py` **cannot be imported** — it opens COM12 at module
   level (:17-21). The demo embeds an **adapted copy** of `GattPeer`
   (house style: 13 standalone scripts duplicate helpers today).
2. `GattPeer._on_read` returns a fixed payload — the copy must serve the
   live plant value.
3. Startup diagnostics stay in the copy even though the 2026-08-22 failure
   is solved — other machines may lack the GATT-server role or hit the
   package-identity restriction: print the full exception + remediation
   hints (Bluetooth on? adapter GATT-server role? USB BLE adapter?) and
   exit non-zero. **Either outcome of a first run on a new machine is a
   deliverable.**
4. No `host_app/` directory exists yet — this feature creates it.

## 4. Design Options & Trade-offs

### Option A — Single self-contained `host_app/run_case.py` (RECOMMENDED)
Everything (runner, adapted GattPeer, first_order case, `CASES` registry,
LLM helper) in one file following the repo's standalone-script convention.
Maximal branch independence (one new file, zero shared modules); split
into a package (`runner.py` / `gatt_peer.py` / `cases/`) when a second
case lands — the contract makes that split mechanical.

### Option B — Package layout now
`run_case.py` + `runner.py` + `gatt_peer.py` + `cases/first_order.py`.
Cleaner seams from day one, more import machinery; justified only when
the second case arrives (reviewer call).

### Option C — Shared `host_app/common.py` with H5.3
Rejected: couples the two parallel AI branches onto one file — exactly the
merge conflict this framework exists to avoid. Convergence refactor may
come later, after both features land.

## 5. Impact Analysis

| File | Kind | Change | Risk |
|------|------|--------|------|
| `host_app/run_case.py` (or package per §4) | new | runner + adapted GattPeer + first_order case + LLM act | low (host-only, new files) |
| `docs/` usage notes | additive | one README section or doc row | low |

**Zero firmware changes. Zero edits to existing scripts.** Disjoint from
H5.3's files → the two features can be AI-generated on parallel branches
and merged in any order.

Location note (review Rec2): the repo root is the legacy H5.1 home —
`llm_loop.py` stays put; `host_app/` is the new home for host tooling;
`llm_loop.py` relocates only on an explicit later decision. Canonical
copy (review Rec3): when the convergence refactor happens, `llm_loop.py`'s
helpers are the source the shared module is derived from.

## 6. Test & Verification Plan

1. `python host_app/run_case.py --help` — CLI sanity, no hardware needed.
2. Full run on COM12 — **two valid outcomes (review R3)**:
   a. GATT data path available: ≥90% of expected conn lines received;
      every line has `ts`/`addr`/`src:"conn"` envelope + `t`/`u`/`y`
      payload fields; physics checkpoint y(t_step+τ) within tolerance of
      63.2%·K. (Verified working on this PC 2026-08-28 —
      `docs/fix-gatt-data-path-2026-08-28.md`.)
   b. Data path unavailable: the run reports the precise reason (peer
      startup exception with remediation hints, or session terminated at
      by-UUID discovery) and exits with a saved transcript — a
      **pass-with-artifact**, not a red test. A legitimately absent or
      restricted WinRT session role must never be scored as failure.
3. `--estimate`: LLM returns parseable JSON estimates; τ_est within ±30%
   of the configured τ (LLM-tolerant margin). Missing API key → skip act,
   clear message, exit 0.
4. Peer-start failure → the full exception is printed and tee'd
   (`… 2>&1 | tee plant_demo_out.txt`) — this transcript is the artifact
   that settles the PC-radio question and feeds the AC-9 peer decision.
5. Ctrl+C mid-run → cleanup (CONN STOP / SCAN STOP), port kept open till
   orderly exit (N3).
6. Regression: none required (no firmware/script changes), but the four hw
   suites remain the reference if anything about the dongle is questioned.

## 7. Open Questions — RESOLVED by review (2026-08-28)

| # | Question | Resolution |
|---|----------|------------|
| 1 | Option A (single file) vs Option B (package now) | **Option A** — split into a package when the second case lands |
| 2 | `--with-scan` default | Stays **opt-in** — keeps the conn demo focused |
| 3 | LLM sample reduction | Send **all 60 lines** (tiny), behind a cap constant (e.g. `LLM_MAX_SAMPLE_LINES`) so future cases are covered |
| 4 | Future-case priority | **`read_only` first** — the only untested data path (poll); notify is already covered by `first_order` |
| 5 | External-peer UUIDs | **Per-case UUIDs** — the case owns its identity |

## 8. Post-Review Path

1. Reviewer sign-off (or amendments) on this document.
2. Promote to `harness/01-features/stage5-host/feature_host_cases.md` as
   **H5.2** (H5.1 template: Basic Info table, numbered ACs, TC table, NFR
   table; header carries Origin = this proposal + review doc).
3. Remove/annotate the corresponding backlog note if any.
4. Implement on `feature/5-host-cases` per the spec → verification §6 →
   status report + `LATEST.md` pointer → squash-merge (keep branch).

## 9. AI Implementation Guidance (for the generating session)

- **Read first:** `CLAUDE.md` → `harness/00-global-context/` (coding
  rules, git workflow) → `harness/01-features/stage5-host/feature_llm_loop_tool.md`
  (H5.1, the host spec template) → this proposal → `tests/hw/test_ble_conn_hw.py`
  (GattPeer :81-222, session probe :339-373, C1 flow :375-416) →
  `vendor_reference/ble_test/DESIGN.md` (the WinRT binding rules the peer
  follows) → `llm_loop.py` (serial + LLM helper conventions).
- **Branch:** `feature/5-host-cases`; never commit to master directly.
- **Commit format:** the 4-section standard (What/Why/How/Verification),
  see `docs/commit-message-standard-2026-08-16.md`.
- **Hard rules:** self-contained files only (no imports from
  `test_ble_conn_hw.py` or `llm_loop.py`); keep port open (N3); never
  notify payloads >253 bytes; single-threaded design; Ctrl+C cleanup on
  every exit path; a peer/session failure is reported with its precise
  reason and a saved transcript (§6.2b), never an exception storm.
- **Done means:** §6 verified end-to-end with a saved transcript.
