# Review — BLE Connection (GATT Client) Feature Proposal

| Field       | Value |
|-------------|-------|
| Date        | 2026-08-16 |
| Reviewer    | Independent code review session (ZCode) |
| Artifact    | `docs/feature-proposal-ble-conn-2026-08-16.md` @ commit `b9053db` **plus the uncommitted working-tree §2.6 "Data Flow" addition** (reviewed in its current form) |
| Branch      | `ble_connected` |
| Scope       | Document review only — no code changed by this review |

---

## Verdict: **APPROVE WITH AMENDMENTS**

The proposal is architecturally sound and unusually well-grounded: its §3
seam analysis was verified against the actual source and **every load-bearing
claim checked out** (see Appendix). Option A (additive `ble_conn.c` inside
the `ble` component + the project's first Kconfig feature flag) is the right
call, Option B adds plumbing without benefit, and the Option C rejection —
generalize the pipeline only when a second Lua-processed source exists — is
correct YAGNI discipline with the refactor trigger properly recorded.

Before promotion to `harness/01-features/stage2-ble-core/feature_ble_conn.md`
(F2.4), **six amendments are required** (§3). They are design-completions,
not objections: the first two prevent a real bug class, the rest close
unspecified behavior that would otherwise be discovered during HIL.

**Note on the task premise:** commit `b9053db` changed **one** feature
requirement document (the proposal) plus two status files
(`status/LATEST.md`, `status/status-2026-08-16-1125.md`). No `harness/`
document was touched — harness promotion is deliberately a post-review step
per the proposal's own §8. This review covers the working-tree version of
the proposal, including the uncommitted §2.6 and the renumbered §8.

---

## 1. What Was Reviewed

- §2 Feature requirement (user stories, PO decisions, defaults, CLI grammar,
  line model, data flow)
- §3–§4 Seam analysis and design options
- §5 Impact analysis
- §6 Test & verification plan
- §7 Open questions (answered in §5 of this review)

Every factual claim about the v1.0.0 firmware in §3/§5 was checked against
the source; results in the Appendix.

## 2. Verified Strengths

1. **The seam analysis is honest and correct.** All seven "anticipated"
   seams exist as claimed, and — more importantly — all four "not
   anticipated" gaps are real: the power lock really is single-holder
   (`power_mgmt.c:93-109`, one `esp_pm_lock` + one `bool`, no refcount);
   no `Kconfig.projbuild` exists anywhere in `firmware/` or `main/`; no
   hook/tap exists in `ble_scan.c` (the queue is the only output); and
   `test_ble_peer_hw.py` does pin the v1 no-connection boundary.
2. **The NimBLE single-discovery constraint is correctly identified**, and
   the raw-report tap is the right shape of answer — additive, no-op when
   unset, keeps "sniff and connect simultaneously" working.
3. **The `-450…-459` error range is genuinely free** (the `-4xx` family
   currently uses `-401/-402/-406/-411`; no `-45x` anywhere in
   `interfaces/` or `firmware/`).
4. **The off-build-as-proof argument is sound.** Making
   `CONFIG_BLE_CONN_ENABLED=n` behave byte-identically to today's firmware
   — verified by the existing suites — is the correct definition of "no
   impact on v1.0.0".
5. **The verification plan is layered like the repo's existing culture**
   (host Unity → HIL → regression → feature-off build → manual runbook →
   runtime health), and the WinRT GATT-server peer with graceful `skip()`
   is realistic about adapter variance.
6. **The uncommitted §2.6 improves the proposal** — the as-is/proposed
   data-flow split and the explicit "deliberately not in the flow" list
   (Lua hooks, pipeline stats) are exactly what a reviewer needs. Commit it.

## 3. Required Amendments (before harness promotion)

### A1. The disconnect side-channel contradicts the derived state machine

§2.6 says disconnect events "return the state machine to IDLE". The CLI has
**no stored state**: `cli_get_state()` derives it from `script_is_running()`
and `ble_scan_is_active()` (`cli_commands.c:90-95`). Two consequences:

- If scanning is active when the connection drops, the derived state is
  **SCANNING**, not IDLE — the proposal's wording would have an implementer
  wrongly stop the scan.
- `SCRIPT RUN` today requires scanning to be active (`cli_commands.c:567+`
  rejects IDLE), so "SCRIPT_RUNNING" already means "scanning + script".

**Amendment:** add an explicit CONN column to the F4.1 state matrix. Suggested:
`CONN START/STOP/TARGET/STATUS/INTERVAL` allowed in IDLE and SCANNING;
rejected in SCRIPT_RUNNING with `-911` (or `-452`) — conn lines bypass Lua
hooks, so running both is harmless but misleading. Connection activity is
orthogonal to the derived enum; no new CLI state value is needed. Specify
what Ctrl+C does when both scan and conn are active (proposal implies
stop-everything — fine, but write it down).

### A2. The emit path can block the NimBLE host task

Notifications arrive as GATT events **in the NimBLE host task** — the same
context as the GAP scan handler, which is spec-bound to return within 1 ms
(F2.2 NFR). The proposed emit path calls `json_encode_conn()` →
`usb_console_send_json()`, and `usb_console_send_line()` takes the TX mutex
with a **100 ms timeout** (`usb_cdc_console.c:44-62`). A congested USB side
(locked port, slow host reader) would stall the BLE stack — and with it,
advertisement handling — for up to 100 ms per line. The 1/s telemetry peer
in the HIL plan will never expose this; a chatty peer will.

**Amendment:** specify a drop policy, consistent with the adv-queue
philosophy ("never block the radio"): either a small conn-payload queue with
a dedicated emitter context (depth ~8, drop-newest + counter, surfaced in
`CONN STATUS`), or a zero-timeout mutex take with drop-and-count. Add a HIL
case (or documented manual test) with a fast-notifying peer.

### A3. Direct connect by address lacks the address type

`ble_gap_connect()` needs the peer address **type** (public / random-static /
RPA). `CONN START <addr>` provides only 6 bytes. Random-static peers — the
majority of embedded devices — will simply fail to connect when initiated
with the public type, and the failure looks like a timeout (`-455`), which
is miserable to debug.

**Amendment:** either add an optional argument (`CONN START <addr>
[public|random]`, default `public`), or auto-learn the type when the address
has been seen by the scan tap (the tap receives `addr_type` for free in
`adv_report_raw_t`). Document the failure mode either way.

### A4. Auto-connect has two blind spots the proposal doesn't state

- **128-bit service UUIDs.** `proto_adv_parse.c` copies only `uuids16` from
  NimBLE's parsed fields — `fields.uuids32`/`uuids128` are discarded
  (ESP32 path, lines ~61-66), and the hand-rolled host path handles only
  AD types 0x01/0x02/0x03/0x08/0x09/0x0A/0xFF. Yet §2.4 advertises
  "16/32/128-bit forms" for `CONN TARGET`. Good news: the tap receives the
  **raw** adv data and can call `ble_hs_adv_parse_fields()` itself, which
  does surface `uuids128` — so 128-bit auto-connect is feasible, but only
  if the tap does its own parse (AD types 0x06/0x07). The amendment: say so,
  and decide whether v2.0 ships 16-bit-only matching with 128-bit parsed
  but matched in the tap.
- **Scan-response-only UUIDs.** The dongle scans **passively** (v1 promise).
  Devices that put their service UUID only in the scan response (common on
  iOS-adjacent and some embedded stacks) are invisible to auto-connect.
  Fixing it means active scanning during peer search — which transmits scan
  requests and changes the product's passive-observer character. That is a
  product decision, not an implementation detail.

**Amendment:** document both limitations in the spec; if active scan during
peer search is desired, make it a separate reviewed decision (config-gated).

### A5. MTU and payload sizing are unstated

`CONFIG_BT_NIMBLE_ATT_PREFERRED_MTU=256` is already set — but it is an
**IDF default, not a deliberate choice** (`sdkconfig.defaults` sets roles
only), so the spec must not silently depend on it. Also unstated:

- Max single notification payload = MTU − 3 (253 B at MTU 256). JSON payloads
  larger than that sent as *multiple* notifications are **not reassembled**
  in v1 — one notification = one emitted line. Say so.
- The >512 B JSON-line truncation policy: the test plan mentions
  "truncation-to-valid-JSON" — the spec should define drop-vs-truncate and
  mark truncated lines (e.g. a `"trunc":true` field) so host tooling can tell.
- Whether the client performs an explicit MTU exchange on connect
  (`ble_gattc_exchange_mtu`) — verify NimBLE's automatic behavior at the
  configured preferred MTU and pin it in the spec.

### A6. `ble_conn` concurrency and init seam

- **Own mutex, from day one.** The codebase already has one lock-coupling
  wart (the pipeline takes the Lua mutex to run the C filter engine,
  `scan_pipeline.c:68` — flagged in
  `docs/evaluation-report-2026-08-15.md` §2.2). Don't repeat the pattern:
  `ble_conn` state is touched from the NimBLE host task (GAP/GATT events)
  and the CLI task (`CONN` commands) — it needs its own lock, and nothing
  else should take it.
- **Init seam missing from the impact table.** `ble_conn` needs an explicit
  initialization point: `ble_conn_init()` called from `app_main` under
  `#ifdef CONFIG_BLE_CONN_ENABLED` (recommended — matches the existing
  boot chain in `main/main.c`), or lazily on first `CONN` command ( harder
  to reason about error paths). Add whichever is chosen to §5.

## 4. Non-Blocking Recommendations

1. **Set `CONFIG_BT_NIMBLE_MAX_CONNECTIONS=1` when the feature is on.**
   The current value, 3, is an IDF default; the feature specifies max 1
   connection — one deliberate line in `sdkconfig.defaults` (next to the
   role flip) saves connection-object RAM and encodes the requirement.
2. **First-Kconfig convention note.** `BLE_CONN_ENABLED` is fine, but record
   in the spec that CMake consumes it as the `CONFIG_` variable directly
   (`if(CONFIG_BLE_CONN_ENABLED) list(APPEND SRCS ble_conn.c)`). Minor
   correction to §3's precedent claim: `LUA_SOURCE` is a conditional
   *dependency* switch, not a conditional-*sources* pattern — the mechanism
   works, the citation is just imprecise.
3. **`test_ble_peer_hw.py` boundary comment goes stale** ("connections are a
   v2 feature; this test pins the v1 boundary"). The test itself still
   passes — a central-role dongle neither advertises nor accepts
   connections — but the comment (and the F2.1 observer-only role table at
   promotion time) should be updated so the docs don't contradict the
   shipped roles.
4. **Document the security limitation** where the no-bonding decision is
   recorded: peers requiring encrypted/paired access return "insufficient
   authentication" reads — unsupported in this version by design.
   (`CONFIG_BT_NIMBLE_SECURITY_ENABLE=y` with `MAX_BONDS=3` already exists,
   so a pairing follow-up is additive, as the proposal says.)
5. **Soak item:** when the conn feature lands, add one
   connect/disconnect/steady-notification cycle to the existing soak script
   alongside the adv traffic.

## 5. Answers to the Proposal's Open Questions (§7)

| # | Question | Reviewer position |
|---|----------|-------------------|
| 1 | No-bonding "just works" acceptable? | **Yes for v2.0 telemetry.** Document the encrypted-peer limitation (§4.4). Bonding would add NVS key lifecycle + pairing UX — correctly deferred. |
| 2 | Conn counters in STATUS? | **Yes, inside the `"conn"` object**; pipeline stats stay adv-only. Add the drop counter from amendment A2 there too. |
| 3 | Merged-line model vs namespacing? | **Merged model is right**, with one addition: envelope keys (`ts`, `addr`, `src`) must **win** on collision — specify the precedence. Consider `"trunc":true` for truncated lines (A5). |
| 4 | Raw tap vs requiring SCAN STOP? | **Raw tap.** Forcing SCAN STOP for auto-connect breaks the core "sniff while probing a peer" workflow. Keep "stop scan first" documented as a fallback simplification if the tap grows complex. |
| 5 | Lua-over-conn requirement? | **None foreseeable in v2.0.** Agree with deferring; the Option-C refactor trigger ("a second line source needing Lua") is correctly recorded. |

## 6. Summary for the Author

The proposal is the strongest kind of change request for this codebase: it
parks scope honestly (feature flag, off-build proof), identifies the true
seams (verified), and knows what it is *not* doing (Option C, bonding,
Lua-over-conn). Land the six amendments — they are one spec-section each,
and A1/A2 would otherwise become field bugs — then promote to F2.4 and
implement in the proposed order.

---

## Appendix: Proposal Claims vs Verified Code

| Proposal claim | Verified fact | Evidence |
|---|---|---|
| `power_hold_activity` is single-holder ("scan is the only assumed holder") | True — one `esp_pm_lock` + one `bool`; second acquire is a no-op, single release frees for all | `power_mgmt.c:43-46, 93-109` |
| No product feature-flag precedent; no `Kconfig.projbuild` exists | True — zero `Kconfig*` files under `firmware/` or `main/` | `find` over both trees |
| `LUA_SOURCE` is a conditional-source precedent | **Imprecise** — it switches a *dependency* (`lua` vs `lua_alt`); SRCS list is fixed. CMake `CONFIG_` variables do provide the needed mechanism | `firmware/components/ble/CMakeLists.txt` |
| Error range `-450…-459` is free | True — `-4xx` uses only `-401/-402/-406/-411`; no `-45x` in `interfaces/` or `firmware/` | grep across both trees |
| Host suite is 86 cases | True — 14+19+14+18+11+10 = 86 `RUN_TEST()` across six suites | `tests/host/test_*.c` |
| `usb_console_send_json` is mutex-serialized, cross-task safe | True — FreeRTOS mutex, but **100 ms take timeout** (basis of amendment A2) | `usb_cdc_console.c:10, 32, 44-62` |
| No tap/hook exists in `ble_scan.c`; queue is the only output | True — GAP DISC handler does dedup → build raw report → non-blocking enqueue, nothing else | `ble_scan.c:137-191` |
| Central role is one config flip away | True — `CONFIG_BT_NIMBLE_ROLE_CENTRAL` not set; observer on; `MAX_CONNECTIONS=3` and `ATT_PREFERRED_MTU=256` already present (IDF defaults, not deliberate — see A5) | `sdkconfig:556-594`, `sdkconfig.defaults:35-42` |
| HIL peer advertises only; connections are v2 | True — P3 comment pins the v1 no-connection boundary | `test_ble_peer_hw.py` header |
| `json_escape_str()` exists for payload embedding | True | `interfaces/json_if.h` |
