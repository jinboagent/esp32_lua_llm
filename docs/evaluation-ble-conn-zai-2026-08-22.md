# Evaluation — zai's F2.4 implementation (`ble_connected_zai` @ ce113e2)

| Field | Value |
|-------|-------|
| Date | 2026-08-22 |
| Evaluator | author of variant A (`ble_connected` @ 90d6a03) |
| Inputs | zai's code (read line-level), his comparison doc
  (`docs/compare-ble-conn-implementations-2026-08-22.md` on his branch),
  his status report, NimBLE source in ESP-IDF v5.1 |
| Method | every divergence claim re-checked against code before judging |

Verdict up front: **zai's variant is the stronger firmware on the two
functional axes that matter (connect-while-scanning, testable failure
codes); variant A is stronger on API hygiene, payload-copy safety, notice
delivery, and GATT-peer test structure. Neither is hardware-proven on the
GATT data path. The right product firmware is a merge, base = B.**

## Divergence-by-divergence

### 3.1 Connect while scanning — B is right, A has a real bug
Verified in NimBLE source: `ble_gap_connect()` returns `BLE_HS_EBUSY`
while discovery is active (`ble_gap.c:5418`). In A, a tap match during a
user scan (or any direct `CONN START <addr>` mid-scan) therefore dies as
"connect failed" — the headline scenario of the feature silently does not
work. A's C0 never exercised a matching tap, so the gap shipped untested.
B's `ble_scan_pause/resume` + 5×30 ms EBUSY retry is the correct seam; all
finalizers (`s_finish_active`, `s_fail_no_conn`, DISCONNECT) resume, and
`ble_scan_stop/start` clear the pause flag.

New nits in B found during this review (not in his doc):
- `ble_scan_resume()` failure leaves `s_scanning==true` with no discovery
  running — a zombie scan. The DISC_COMPLETE path clears the flag on
  restart failure; resume should do the same.
- `s_paused` is a plain `bool` shared with the host task, inconsistent
  with the file's own `atomic_bool` discipline (benign on ESP32, still).
- Product trade-off to document: while paused, the adv stream has a hole
  (up to ~8 s for a blocked direct connect). B's trade (adv hole vs A's
  dead connect) is clearly better for this product, but the hole should
  be visible in the spec.

### 3.2 Synchronous direct start — B better for tests, A better for UX
B blocks the CLI ≤ ~8 s and returns the real code (`-455` verified on
hardware); A is fire-and-forget with notice lines only. B's choice makes
the error matrix machine-testable (his C0 proves it). UX cost he discloses:
no Ctrl+C during the wait. Merge refinement neither has: wait in short
slices and honor an interrupt flag, keeping both properties.

### 3.3 Length-delimited payload — B wins
`(payload, len)` end-to-end (cap 253) is correct by construction. A's
C-string path would silently truncate binary payloads at the first NUL
inside `json_escape_str`. Adopt B's signature.

### 3.4 Notification filtering — B wins
`attr_handle == s_chr_val` filter vs A's accept-anything-while-ACTIVE.
With a multi-characteristic peer, A would interleave unrelated
notifications into the same line shape. Adopt B.

### 3.5 Locking & priority — both defensible; take B, keep the question open
Single mutex (B) is easier to audit than A's atomics+mutex mix; per-notify
mutex takes in the host task are irrelevant at telemetry rates. Worker
priority 2 (B, radio-first) is the safer default; A's priority-8 emitter
only wins in an unproven USB-saturation scenario. Take B; revisit only if
a soak shows USB starvation.

### 3.6 Observability — split the point
B's `ble_conn_status_t` (connects/disconnects/rx_notify/rx_read/tx_lines/
errors/mtu/subscribed/polling) is the better field-debug surface; A's
named `BLE_CONN_ERR_*` constants are the better API hygiene (he agrees).
Merge = B's struct + A's constants.

## Smaller deltas (his §4, re-checked)

| Area | Verdict |
|---|---|
| MTU pinning | A's explicit `ble_att_set_preferred_mtu(256)` nicer; take A |
| Envelope collision | B drops colliding members, A wraps whole payload. B kinder to hosts, A safer; B's scanner is pinned by his host tests — either acceptable, keep strict tests if B's is adopted |
| Notices | A's notice-evicts-oldest beats B's droppable op-queue notice; take A |
| Addr-type learn | B's learn-on-demand + explicit arg is simpler than A's speculative 8-entry cache; take B |
| Char auto-pick | B's doc claims notify>indicate>read precedence; his code is first-wins-overall (chain gates every class on `!s_chr_found`). Doc/code mismatch, cosmetic (explicit UUID is the precise path) |
| `om->om_data` | B reads only the first mbuf with full `OS_MBUF_PKTLEN` length; A's `os_mbuf_copydata` is correct for chained mbufs. Single-mbuf is near-certain at ≤253 B, but take A's call |

## Test depth

B: +22 host tests (control plane included) and C0 29/29 on device incl.
`-455` and Ctrl+C mid-search — deeper than A's +9 / 14. A: the only
WinRT GATT-server C1–C6 structure (skipped here, but portable). Merge =
B's host/C0 depth + A's C1–C6 suite. Both caught two encoder bugs
pre-flash; both regression matrices green; B additionally recorded the
~163 KB on/off flash delta.

## Recommended merge (if the owner wants one)

Base **B**, then cherry-pick from A: named error constants;
`os_mbuf_copydata`; notice-evicts-oldest; explicit preferred-MTU; the
WinRT C1–C6 suite; and fix B's nits (zombie scan on resume failure,
atomic `s_paused`, sliced Ctrl+C-aware wait in direct start). Settle
§3.1 definitively with C1–C6 against a real peripheral (ATC sensor or
nRF Connect).

## Fairness note

This evaluation was written by the author of variant A; the convergent
sections of zai's comparison doc were accepted only after line-level
re-verification, and every criticism above cites code. The two variants
validate each other: 80% structural convergence from the same reviewed
proposal is evidence the review amendments were implementation-
independent requirements.

## Addendum — improvement pass executed (same day)

The product owner directed: apply the merge to `ble_connected_zai`,
leave `ble_connected` untouched. Done and re-verified on this branch:

- All §"Recommended merge" items landed, including two refinements made
  while implementing: the Ctrl+C-aware wait uses a new pushback-safe
  `usb_console_poll_interrupt()` in the usb component (no separate RX
  task exists, so an RX-side hook was impossible; the one-byte lookahead
  preserves the line stream), and on `-457` the CLI runs the normal
  `h_interrupt` semantics so interrupt behavior stays uniform.
- B nits fixed (zombie scan on resume failure, atomic `s_paused`).
- C1–C6 WinRT tier ported into this branch's HIL suite.
- Re-verification: host 108/108 (`-Werror` clean), C0 29/29 on device
  (incl. `-455` sync timeout and Ctrl+C mid-search — the sliced wait did
  not regress them), regressions 32/14/45/11, on/off builds clean.

Judgment stands: base-B + these cherry-picks is the strongest firmware;
the remaining gap (AC-9, real-peer GATT data path) is unchanged and now
automatically covered wherever WinRT GATT servers work.
