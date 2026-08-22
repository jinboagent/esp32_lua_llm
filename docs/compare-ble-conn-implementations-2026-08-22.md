# F2.4 Implementation Comparison — `ble_connected` vs `ble_connected_zai`

| Field | Value |
|-------|-------|
| Date | 2026-08-22 |
| Variant A | branch **`ble_connected`** — commit `5baa6a7` ("feat(ble): F2.4 … with review amendments") |
| Variant B | branch **`ble_connected_zai`** — commit `ce113e2` (independent implementation, this session) |
| Common base | `b9053db` (the reviewed proposal) — both variants had the same proposal + the same two review documents as input |
| Method | B was written **without reading A**; this comparison was produced only after B was complete and verified. The product owner is expected to read the code side by side; this document is the map. |

---

## 1. Headline

The two implementations are **~80% convergent in structure** — same module
placement, same seams, same state-machine shape, same CCCD-subscribe chain,
same line model — which is what you expect when both start from the same
proposal and absorbed the same review amendments (A1–A6, D1–D7). The
differences that matter cluster into six design decisions, two functional
gaps in A, one deliberate trade-off in B, and a test-depth gap (B's suites
are 2–3× larger). If the two were merged, the strongest result takes A's
ergonomics (named error constants, leaner info struct, notice-evicts-oldest,
explicit `ble_att_set_preferred_mtu`) onto B's coexistence machinery
(scan pause/resume + EBUSY retry), B's synchronous direct-start, and B's
binary-safe payload path.

## 2. Convergent decisions (both variants, driven by the shared inputs)

| Aspect | Both did |
|---|---|
| Placement | `ble_conn.c` inside the `ble` component; API appended to `interfaces/ble_if.h`; error range `-450..-456` with identical meanings |
| Build flag | `Kconfig.projbuild` `BLE_CONN_ENABLED` default y, `select BT_NIMBLE_ROLE_CENTRAL`; CMake source gating; `sdkconfig.defaults` += `MAX_CONNECTIONS=1`; CONN answers `-451` when off |
| Scan seam | `ble_scan_set_tap(cb)` invoked **pre-dedup on every DISC event** (A: `ble_scan.c` diff; B: `ble_scan.c:175-183`) |
| UUID matching (A4) | Own `ble_hs_adv_parse_fields` walk comparing 16/32/128-bit service UUIDs — proto layer bypassed in both |
| Emit path (A2) | GATT callbacks only enqueue; a dedicated task does every USB write — the NimBLE host task never blocks on the TX mutex; queue depth 8, drop-newest + counted |
| Subscribe chain | `disc_svc_by_uuid` → `disc_all_chrs` → `disc_all_dscs` (0x2902) → CCCD write (0x0001/0x0002) → ACTIVE; failures → terminate `-454`; poll fallback via `ble_gattc_read`; "one payload = one line, no reassembly" (A5) |
| Power (A8/D1) | `power_hold_conn` OR-ed with `power_hold_activity` over one `esp_pm` lock; 40 mA connected estimate; wired through an event callback in `main.c` (no ble→power dependency) |
| Line model | Shared **pure-C** `json_encode_conn` in `json_encoder.c` (host-testable, no cJSON), merge with envelope precedence for ts/addr/src, wrap + escape otherwise, `trunc:true` when the 512 B line overflows |
| CLI matrix (A1) | CONN allowed in IDLE/SCANNING; in SCRIPT_RUNNING only STOP/STATUS (others `-911`); Ctrl+C also disconnects; STATUS gains an additive `conn` object (`enabled:false` in off-builds) |
| UUID parsing | Hand-rolled string→`ble_uuid_any_t` (no `ble_uuid_init_from_str` in this NimBLE), 128-bit stored reversed — both hit and solved the same byte-order trap |

## 3. The six real divergences

### 3.1 Connect while a scan is running — the biggest functional difference

NimBLE runs **one GAP procedure**: `ble_gap_connect` conflicts with an
active discovery (`BLE_HS_EBUSY`).

- **A** (`ble_connected:ble_conn.c:448-460`): `s_connect_to_peer()` calls
  `ble_gap_connect` directly from the emitter task — no scan pause, no
  EBUSY retry. A solves the *discovery* half of the conflict (own search
  only when the scan is idle, tap otherwise, comment at :671-673) but not
  the *connect* half: when the tap finds the peer while a user scan runs —
  or on any direct `CONN START <addr>` during a scan — the connect call
  returns EBUSY and is reported as "connect failed" (state → idle). Their
  peerless C0 never fired the tap against a matching peer, so this path
  was never exercised on hardware.
- **B** (`ble_conn.c` `s_worker_connect` + `ble_scan.c`
  `ble_scan_pause/resume`): the scan's discovery is **paused around the
  connect** (a `s_paused` flag also suppresses the N2 DISC_COMPLETE
  restart) and resumed by the connect/disconnect finalizers; the worker
  additionally retries `ble_gap_connect` 5 × 30 ms on EBUSY. The user
  scan always survives connect churn.
- **Caveat both ways**: neither variant has yet talked to a real
  peripheral, so "connect succeeds while scanning" is design-verified (B)
  vs unhandled (A) rather than hardware-proven. B's pause/resume machinery
  itself is exercised by C0's coexistence section.

### 3.2 Direct `CONN START` semantics: synchronous vs fire-and-forget

- **A**: fully async. The CLI returns immediately; failures arrive later
  as `{"cmd":"conn_event"}` notice lines. The CLI task is never blocked —
  but a host cannot synchronously assert `-455` (their C0 cannot test the
  timeout path; error codes surface indirectly).
- **B**: direct connects block the CLI until up/failed (bounded by the
  6 s link timeout + retries ≈ 8 s worst case), returning the real code.
  Verified on hardware (unreachable address → `-455` in ~6 s). Trade-off:
  during the blocked wait the CLI cannot process other input — including
  Ctrl+C (bounded, but a real UX cost to know about).

### 3.3 Payload handling: C-string vs length-delimited

- **A**: payloads are NUL-terminated at enqueue (`it.buf[len] = '\0'`,
  cap 255) and `json_encode_conn` takes a C string. Simpler; assumes
  textual peers.
- **B**: `(payload, len)` end to end (cap 253 = MTU 256 − 3); binary-safe
  by construction, length embedded in the queue item.
- Related nit: A's `BLE_CONN_PAYLOAD_MAX_LEN` is 256; payloads above
  MTU−3 cannot arrive via ATT anyway, so the difference is cosmetic.

### 3.4 Notification filtering

- **A** (`ble_conn.c:537-541`): accepts **any** notification while ACTIVE
  — a peer with several notifying characteristics would stream all of
  them through the same `"src":"conn"` shape.
- **B**: filters `notify_rx.attr_handle == s_chr_val` — only the
  subscribed characteristic emits lines.

### 3.5 Locking style

- **A**: `atomic_int` state + atomics for counters + a mutex for config —
  cheap reads, two mechanisms.
- **B**: one mutex for all mutable state, lock-free enum reads (atomic on
  this target), counters under the mutex — one mechanism, slightly
  costlier counter updates (irrelevant at telemetry rates).
- Also: A's emitter task runs at **priority 8** (above the adv pipeline's
  2); B's worker at 2 (equal to the pipeline, below the NimBLE host).
  A's choice favors USB drain under load; B's keeps the radio-first
  priority ordering. Defensible either way — worth a conscious decision
  when merging.

### 3.6 Observability surface

- **A**: lean `ble_conn_info_t` (state, addr, connected, notify_mode,
  poll_ms, rx_lines, drops). `CONN STATUS` nests a conn object.
- **B**: full `ble_conn_status_t` (+ connects, disconnects, rx_notify,
  rx_read, tx_lines, errors, mtu, subscribed/polling). More counters, all
  surfaced flat in `CONN STATUS`.
- A's `interfaces/ble_if.h` defines **named constants**
  (`BLE_CONN_ERR_TIMEOUT` etc.) — B documents the numbers in comments
  only. A's is better practice; B should adopt it.

## 4. Smaller differences worth knowing

| Area | A | B |
|---|---|---|
| MTU pinning | `ble_att_set_preferred_mtu(256)` at init — explicit (nicer) | relies on exchange + existing config; spec notes it |
| Envelope collision in merge | payload object containing ts/addr/src → **whole payload wrapped** | colliding members dropped, rest merged |
| Notices (disconnect events) | separate queue kind; **notices evict the oldest** data item (nice) | via the 4-deep op queue; could be dropped under extreme load (weaker) |
| Addr-type learn (A3) | 8-entry cache of every seen address | learn-on-demand for the pending direct address + explicit `public\|random` CLI arg |
| Char auto-pick | first char with notify\|indicate\|read (UUID filter optional) | notify > indicate > read precedence, first-wins per class |
| Own peer-search restart | explicit window (160/80) | `BLE_HS_FOREVER` + DISC_COMPLETE restart (mirrors the N2 fix) |
| State naming | `BLE_CONN_STATE_IDLE` | `BLE_CONN_STATE_OFF` |
| Event callback | enum events (`BLE_CONN_EVT_CONNECTED/...`) — extensible | `(connected, addr)` — simpler |

## 5. Test & verification depth

| Tier | A (`ble_connected`) | B (`ble_connected_zai`) |
|---|---|---|
| Host Unity | +9 (95/95) | **+22 (108/108)** — encoder merge/wrap/trunc/escapes/binary + CONN family + state matrix + STATUS object |
| C0 HIL (peerless) | 14 checks | **29 checks** — incl. `-455` timeout on an unreachable address, Ctrl+C abort mid-search, scan coexistence bookkeeping, adv-stream intactness |
| Peer suites (C1–C6) | structured for WinRT GATT server, skipped (unavailable) | documented as open; ATC sensor pilot is the intended real peer |
| Regressions | 32/14/45/11 green | 32/14/45/11 green |
| On/off builds | on + off clean | on (771 KB) + off (604 KB) clean, ~163 KB feature cost recorded |
| Known caught bugs | "two encoder bugs caught pre-flash" (their commit) | two encoder bugs caught by host tests (string-value scanner ate its opening quote; truncation budget one byte short) — same class, both fixed pre-flash |

## 6. If the product owner merges the two

Take from **A**: named error-code constants; `ble_att_set_preferred_mtu`;
notice-evicts-oldest queue semantics; the leaner `conn` STATUS fragment;
priority-8 emitter *if* USB saturation is ever demonstrated.
Take from **B**: the scan pause/resume seam + EBUSY retry (§3.1 — A needs
this for "connect while scanning" to work at all); synchronous direct
start (or A's async plus a wait-for-result CLI option); char-handle
notification filter; binary-safe payload path; the fuller counters.
Then run C1–C6 against the ATC sensor — both variants need it, and it will
settle §3.1 definitively on hardware.

## 7. Fairness notes

- Both variants were written by AI agents from the same proposal and the
  same two review documents; the convergence above validates the review
  amendments as implementation-independent requirements, and the
  divergences show exactly where the spec left room (§3 of the proposal
  did not prescribe the connect-vs-scan mechanism).
- A's C1–C6 suite structure is genuinely ahead of B's here (B pointed at
  the sensor pilot instead of writing a WinRT-server suite); if the WinRT
  path ever becomes available, porting A's suite to B's firmware is cheap.
- This document lives on `ble_connected_zai` (`docs/`); reading it
  alongside `git diff ble_connected ble_connected_zai -- firmware/` gives
  the full line-level picture.
