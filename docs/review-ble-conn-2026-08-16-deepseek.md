# Independent Review — BLE Connection (GATT Client) Feature Proposal

| Field       | Value |
|-------------|-------|
| Date        | 2026-08-16 |
| Reviewer    | DeepSeek session (independent review) |
| Artifact    | `docs/feature-proposal-ble-conn-2026-08-16.md` (working tree, incl. §2.6) |
| Branch      | `ble_connected` |
| Scope       | Document review only — no code changed by this review |

> A prior review exists at `docs/review-ble-conn-2026-08-16.md` (verdict
> APPROVE WITH AMENDMENTS, six amendments A1–A6). This document is a
> **separate, independent** review written from a fresh read of the proposal
> and the cited source. It confirms the prior review's load-bearing claims
> and adds one finding the prior review did not surface (finding D1 below).

---

## Verdict: APPROVE WITH AMENDMENTS

The proposal is a well-grounded, review-ready change request. Its §3 seam
analysis is honest and — I verified this directly against source — every
claim it rests on is accurate. The core judgment call, **Option A (additive
`ble_conn.c` inside the `ble` component + the project's first Kconfig flag)
over Option C (redesign the pipeline now)**, is correct YAGNI discipline:
the recorded refactor trigger ("a second line source needs Lua processing")
is exactly the right boundary to wait for.

Seven items should be resolved before harness promotion. Six are design
completions already captured by the prior review (they would surface as
field bugs during HIL); one — **D1, power observability** — is new.

---

## 1. Verification of load-bearing claims

I read the proposal against the current source. Every seam it cites exists as
described:

| Claim | Verified | Evidence |
|---|---|---|
| `power_hold_activity` is single-holder, no refcount | ✅ | `power_mgmt.c:93-109` — one `esp_pm_lock` + one `bool`; a second holder would be lost on a single release |
| Central role is one config flip away | ✅ | `sdkconfig.defaults:41` `CONFIG_BT_NIMBLE_ROLE_CENTRAL=n` |
| No `Kconfig.projbuild` exists — first of its kind | ✅ | none under `firmware/` or `main/` |
| `json_escape_str()` available for payload embedding | ✅ | `interfaces/json_if.h:60` |
| Error range `-450…-459` free | ✅ | `-4xx` uses only `-401/-402/-406/-411` |
| NimBLE runs one discovery at a time → raw-tap design | ✅ | correct constraint; tap is the right additive shape |
| CLI derives state, stores none | ✅ | `cli_commands.c:90-95` `cli_get_state()` |
| USB TX path mutex has a **100 ms** take timeout | ✅ | `usb_cdc_console.c:44-62` |

The "off-build is the proof of no impact" argument (§4, §6.4) is the
strongest single idea in the proposal: `CONFIG_BLE_CONN_ENABLED=n` behaving
byte-identically to today's firmware, verified by the existing suites, is the
correct definition of "no regression."

---

## 2. Findings

### D1. Power observability is conn-unaware *(new — not in the prior review)*

The proposal's §3 gap #1 correctly identifies that `power_hold_activity` is
single-holder and proposes `power_hold_conn()` OR-ed into the same lock. But
that fixes only the **sleep lock**. The two observability functions derive
state *solely* from scan/script activity and were not mentioned in the impact
table:

```c
power_state_t power_get_state(void) {              // power_mgmt.c:120-127
    if (ble_scan_is_active() || script_is_running()) return POWER_STATE_ACTIVE;
    return s_config.sleep_enabled ? POWER_STATE_LIGHT_SLEEP : POWER_STATE_ACTIVE;
}
int power_get_current_ma(uint32_t *ma) {           // power_mgmt.c:129-143
    if (ble_scan_is_active())      *ma = 45;
    else if (sleep_enabled)        *ma = 8;
    else                           *ma = 30;
}
```

With a connection active and scan **off**, `power_hold_conn(true)` keeps the
SoC awake, but `POWER STATUS` (via `h_power`, `cli_commands.c:627-643`)
reports `light_sleep` / 8 mA while the radio is actually held on at ~30–45 mA.
`POWER STATUS` is precisely the command a user runs to debug a chatty peer, so
this wrong reading lands exactly where it hurts.

**Required:** extend `power_get_state()` and `power_get_current_ma()` to treat
an active connection as ACTIVE (a `ble_conn_is_active()` primitive, the same
one `power_hold_conn` uses), and add a HIL assertion that `POWER STATUS`
reports `active` while a connection streams. Also add a `power_mgmt.c` row to
the §5 impact table — currently only the lock is listed, not the observability.

### D2. The merged-line model lets payload keys shadow the envelope

§2.5 merges payload JSON fields directly into the line envelope:
`{"ts":…,"addr":"…","src":"conn",<payload fields>}`. A peer (or a malformed
payload) emitting `{"ts":0,"addr":"00:00:00:00:00:00"}` would overwrite the
dongle's own `ts`/`addr` on merge. **Envelope keys must win on collision**, and
the precedence must be written down; otherwise host tooling that keys on
`addr` will misattribute connection data. This is the prior review's open
question #3 — I am promoting it from "reviewer position" to a **required**
precedence rule because it is a correctness, not a style, issue.

### D3. Emit path can block the NimBLE host task

Notifications arrive as GATT events **inside the NimBLE host task** — the same
context as the GAP scan handler, which is bound to return in ~1 ms. The
proposed emit path (`json_encode_conn` → `usb_console_send_json`) reaches
`usb_console_send_line`, which takes the TX mutex with a **100 ms** timeout
(`usb_cdc_console.c:44-62`). A congested USB side stalls the BLE stack — and
advertisement handling — up to 100 ms per line. A 1/s HIL peer will never
expose this; a chatty peer will.

**Required:** adopt the adv-path philosophy ("never block the radio"): a
small conn-payload queue with a dedicated emitter context (or zero-timeout
mutex take with drop-and-count surfaced in `CONN STATUS`), plus a HIL case
with a fast-notifying peer.

### D4. `CONN START <addr>` lacks the address type

`ble_gap_connect()` needs the peer address **type** (public / random-static /
RPA). Six bytes is not enough; random-static peers — the majority of embedded
devices — will fail as a misleading `-455` timeout when attempted as public.

**Required:** add an optional `[public|random]` argument (default `public`),
or auto-learn the type from the scan tap (the raw report already carries
`addr_type`). Document the failure mode either way.

### D5. Two auto-connect blind spots are unstated

- **128-bit service UUIDs.** `proto_adv_parse.c` copies only `uuids16`
  (`fields.uuids32`/`uuids128` are dropped on the ESP32 path), yet §2.4
  advertises "16/32/128-bit forms" for `CONN TARGET`. The tap receives raw adv
  data and can call `ble_hs_adv_parse_fields()` itself to surface `uuids128`,
  so 128-bit matching is feasible — but only if the tap parses its own data.
  State this, and decide whether v2.0 ships 16-bit-only.
- **Scan-response-only UUIDs.** The dongle scans passively (v1 promise).
  Devices that advertise their service UUID only in the scan response are
  invisible to auto-connect. Closing this means active scanning during peer
  search — which transmits scan requests and changes the passive-observer
  character. That is a product decision, not an implementation detail; gate it.

### D6. MTU and payload sizing are unstated

`CONFIG_BT_NIMBLE_ATT_PREFERRED_MTU=256` is an IDF default, not a deliberate
choice. Unstated: max single-notification payload (MTU−3 = 253 B); that
multi-notification payloads are **not reassembled** (one notification = one
line); the >512 B truncation policy (drop-vs-truncate, and a `"trunc":true`
marker); and whether `ble_gattc_exchange_mtu` is called explicitly. Pin each
in the spec.

### D7. `ble_conn` concurrency + init seam

- **Own mutex from day one.** `ble_conn` state is touched from the NimBLE host
  task (GAP/GATT events) and the CLI task (`CONN` commands). The codebase
  already has one lock-coupling wart (pipeline taking the Lua mutex to run the
  C filter, `scan_pipeline.c:68`); don't repeat it.
- **Init seam missing from the impact table.** Add `ble_conn_init()` called
  from `app_main` under `#ifdef CONFIG_BLE_CONN_ENABLED` to §5.

---

## 3. Non-blocking recommendations

1. Set `CONFIG_BT_NIMBLE_MAX_CONNECTIONS=1` when the feature is on (current
   value 3 is an IDF default; the feature specifies max 1).
2. Correct §3's precedent citation: `LUA_SOURCE` is a conditional-*dependency*
   switch, not a conditional-*sources* pattern — the mechanism works, the
   wording is imprecise.
3. Update the `test_ble_peer_hw.py` "connections are v2" boundary comment and
   the F2.1 observer-only role table at promotion time so docs don't
   contradict shipped roles.
4. Document the no-bonding security limitation: encrypted/paired peers return
   "insufficient authentication" reads — unsupported by design in this version.
5. Add a connect/disconnect/steady-notification cycle to the soak script when
   the feature lands.

---

## 4. Answers to the proposal's open questions (§7)

| # | Question | Position |
|---|----------|----------|
| 1 | No-bonding acceptable? | Yes for v2.0 telemetry; document the encrypted-peer limitation. |
| 2 | Conn counters in STATUS? | Yes, inside the `"conn"` object; pipeline stats stay adv-only; include the D3 drop counter. |
| 3 | Merged vs namespaced line model? | Merged, with envelope keys (`ts`,`addr`,`src`) winning on collision — see D2 (required, not optional). |
| 4 | Raw tap vs SCAN STOP for auto-connect? | Raw tap; keep "stop scan first" as a documented fallback. |
| 5 | Lua-over-conn requirement? | None foreseeable in v2.0; Option-C trigger correctly recorded. |

---

## 5. Summary

The proposal is a model change request: it parks scope honestly (feature flag,
off-build proof), names the true seams (verified), and states what it is *not*
doing (Option C, bonding, Lua-over-conn). Resolve the seven findings — D1 and
D2 are the two that would otherwise ship as real defects — then promote to
`harness/01-features/stage2-ble-core/feature_ble_conn.md` (F2.4) and implement
in the proposed order.
