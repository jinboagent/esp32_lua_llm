# Combined Review — H5.2 (Host Cases) & H5.3 (Assistant Session)

| Field       | Value |
|-------------|-------|
| Date        | 2026-08-28 |
| Reviewer    | DeepSeek session (independent review) |
| Artifacts   | `docs/feature-proposal-host-cases-2026-08-28.md` (H5.2), `docs/feature-proposal-assistant-session-2026-08-28.md` (H5.3) |
| Branch      | `feature/5-host-cases` (H5.2), `feature/5-assistant` (H5.3) — at implementation |
| Scope       | Document review only — no code changed by this review |

---

## Verdict: APPROVE WITH AMENDMENTS (both)

Both proposals are well-grounded and follow the same review-first discipline as
the earlier BLE-connection proposal. Every load-bearing claim was verified
against source and holds. The right architectural call — **parallel, disjoint,
self-contained files; no shared `common.py`** — is correct for two AI branches
that must merge in any order with zero conflicts.

Three amendments are **required** before harness promotion (R1–R3); three are
**non-blocking** (Rec1–Rec3). R1 is a design-defect that would otherwise be
discovered only after the implementing session has already built the wrong
thing.

---

## 1. Verification summary (claims vs code)

| Claim | Verdict | Evidence |
|---|---|---|
| `llm_loop.py` (H5.1): `--goal`, `cmd_json` @ :88, `upload_script`, `.llm_env` resolution, `LLM_MODEL` default `gpt-4o-mini`, urllib POST, temp 0.2, timeout 120 | ✅ | root `llm_loop.py` (381 lines); `cmd_json` :88, `llm_generate` :202-230, `load_env_file` :187-199 |
| `SCRIPT RUN` inside `upload_script` | ⚠️ nuance | `upload_script` (:135-146) does `SCRIPT LOAD`→paced lines→`SCRIPT END` only; `SCRIPT RUN` is a separate call in `do_deploy`/`do_loop`. Proposal's phrasing is fine, but the implementation guidance should cite the exact split |
| `harness/01-features/stage5-host/feature_llm_loop_tool.md` (H5.1) exists | ✅ | sole file in `stage5-host/` today |
| `host_app/` does not exist yet | ✅ | confirmed absent; both proposals correctly create it |
| F2.4 `ble_conn.c` + `CONFIG_BLE_CONN_ENABLED` Kconfig + `CONN` CLI family + `json_encode_conn` merge → `"src":"conn"` + poll 1000 ms + bypasses Lua hooks | ✅ | `ble_conn.c` (36 KB), `Kconfig.projbuild:3-7`, `cli_commands.c` CONN handlers :698-820, `json_encoder.c:299-357`, `ble_if.h:97` |
| `test_ble_conn_hw.py`: `GattPeer` :76-139, C1 :247-295, `SVC_UUID`/`CHAR_UUID`, module-level `COM12` open (unimportable), WinRT GATT SKIP | ✅ | `tests/hw/test_ble_conn_hw.py:17-21, 73-74, 76-139, 246-295` |
| `vendor_reference/ble_test/` reference WinRT peripheral exists | ✅ | root `vendor_reference/ble_test/` |

Both proposals describe the existing code accurately; the only thing they
reference that does not yet exist is the `host_app/` directory itself — expected,
since both are review-first documents.

---

## 2. H5.3 (Assistant Session) — findings

### R1 (required) — `input()` contradicts "continuous drain while the user thinks"

§2.3 responsibility 1 requires *"a reader that drains the serial port while the
user thinks, so the device is never left blocked"*, and the diagram labels the
loop a message Prompt. But §3 gap 1 specifies an *"`input()`-based console loop …
single process, no threads."* These are incompatible: `input()` **blocks the
thread** until Enter, so the serial port is *not* drained while the user
types/ponders — the exact failure the requirement exists to prevent.

A single-threaded message Prompt that multiplexes serial + user input needs
**non-blocking stdin** (`msvcrt.kbhit()`+`getwch()` on Windows,
`select([sys.stdin],…)` on POSIX) combined with short serial timeouts — *not*
`input()`. "Continuous drain" and "no threads" only coexist via non-blocking
stdin, and the current wording would steer the implementing session straight to
a blocking loop that silently violates its own N3 keep-open guarantee.

**Amendment:** rewrite §2.3/§3 to state the stdin mechanism explicitly
(non-blocking polling on the target platform), and strike `input()` as the
described mechanism.

### R2 (required) — feature-off firmware is not handled

`/conn on|off` (§2.5) maps to `CONN START/STOP`. Against a firmware built with
`CONFIG_BLE_CONN_ENABLED=n` — the minimal-sniffer build the F2.4 proposal
explicitly protects — these return `-451 "not compiled in"`. The assistant is a
host program with no knowledge of the device's build config; it must degrade
gracefully (report "conn not enabled on this firmware") rather than mis-report
success or hang.

**Amendment:** one sentence in the spec defining the `-451` handling on `/conn`.

### Rec1 (non-blocking) — the `lua` envelope is the fragile JSON path

`{"type":"lua","code":"-- Lua…"}` embeds a script full of quotes/newlines/
backslashes inside a JSON string — the single most likely place the LLM emits
invalid JSON. The proposal already has "one retry with a corrective
instruction," but should call out that `lua.code` is the highest-risk field and
specify either an escaping-focused corrective instruction or a fenced-code
(``` ```lua ```) fallback extraction when strict JSON parse fails.

---

## 3. H5.2 (Host Cases) — findings

### R3 (required) — the notify path is unproven; success criteria should say so

`GattPeer.notify()` / `wait_subscribed()` have **never run** — C1–C6 were
skipped because the WinRT GATT-server startup failed 2026-08-22. The demo *is*
the first exercise of the real GATT data path. The proposal is honest about this
(§1, §3 gap 3: "either outcome is a deliverable"), but §6.2's success criterion
(≥90% lines + physics checkpoint) reads as pass/fail.

**Amendment:** reframe §6.2 so the **first run's valid outcome is the diagnostic
itself** — if peer startup throws, the tee'd exception (with remediation hints)
is a pass-with-artifact, not a failure. Otherwise a legitimately absent WinRT
server role would be scored as a red test.

### Rec2 (non-blocking) — host-tooling location is inconsistent

H5.1 lives at **root** (`llm_loop.py`); H5.2/H5.3 will live in **`host_app/`**.
Both proposals correctly promise "don't touch `llm_loop.py`," so the split
persists. Add one line stating the intent (root = legacy H5.1; `host_app/` = the
new home; `llm_loop.py` moves only on an explicit later decision) so a future
reader doesn't "fix" the inconsistency by relocating files.

### Rec3 (non-blocking) — record the canonical-copy target for convergence

Both proposals reject a shared `common.py` now (correct — parallel branches, zero
merge conflict) but defer a "convergence refactor." When it happens, three
near-identical helpers exist (`cmd_json`/serial, `.llm_env` resolution, the LLM
POST helper, plus H5.2's adapted `GattPeer`). Note in each proposal *which* copy
is canonical so the eventual DRY pass has a target rather than re-deciding.

---

## 4. Answers to the open questions

**H5.3:**
1. History trimming — trim by **token-budget cap**, not turn count; it matches the model's real context limit.
2. Device buffer — per-plane caps; sample adv, keep all recent conn.
3. Session persistence — yes, tee per run (already in §6.6).
4. `request_data` type — defer to v2.
5. `/deploy <file.lua>` — nice-to-have, low priority.

**H5.2:**
1. Option A (single file) now; split into a package when the second case lands.
2. `--with-scan` stays opt-in (keeps the conn demo focused).
3. Send all 60 lines to the LLM (tiny), but add a cap constant for future cases.
4. Future-case priority — **`read_only` first** (it is the only *untested* data path; notify is covered by the first_order case).
5. Per-case UUIDs — the case owns its identity.

---

## 5. Summary

Both proposals are the right shape of change request for this codebase: they park
scope honestly, cite real seams (verified), and know what they are *not* doing
(shared module, firmware changes). Land the three required amendments — R1 would
otherwise steer the implementing session into a blocking loop, and R3 would
mis-score a legitimate hardware absence — then promote to
`harness/01-features/stage5-host/` and implement on the stated branches.
