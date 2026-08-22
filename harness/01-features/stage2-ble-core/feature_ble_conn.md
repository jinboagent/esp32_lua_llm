# Feature: BLE Connection (Optional GATT Client)

| Field | Value |
|-------|-------|
| **Feature ID** | F2.4 |
| **Stage** | 2 — BLE Core |
| **Layer** | BLE Core |
| **Dependencies** | F2.1 (NimBLE init), F2.2 (BLE scan), F1.2 (json encoder), F4.1 (CLI), F4.3 (power) |
| **Source Files** | `interfaces/ble_if.h`, `interfaces/json_if.h`, `interfaces/power_if.h`, `firmware/components/ble/ble_conn.c`, `ble_scan.c` (tap/pause seams), `ble/Kconfig.projbuild`, `firmware/components/json_enc/json_encoder.c`, `firmware/components/power/power_mgmt.c`, `firmware/components/cli/cli_commands.c`, `main/main.c` |
| **Build flag** | `CONFIG_BLE_CONN_ENABLED` (default y; `select BT_NIMBLE_ROLE_CENTRAL`) |
| **Test Files** | `tests/host/test_ble_conn.c` (22 cases), `test_ble_conn_hw.py` (C0, 29 checks) |
| **Origin** | Promoted from `docs/feature-proposal-ble-conn-2026-08-16.md` after review (`docs/review-ble-conn-2026-08-16.md`); this spec reflects the implementation on branch `ble_connected_zai`. |

---

## 1. Description

Optional central-role connection: connect to ONE peer — by advertised
service UUID (auto-connect) or by direct address — subscribe to a
notify/indicate characteristic (poll fallback), and re-stream the payloads
as `"src":"conn"` JSON lines on the same USB stream as advertisements.
Existing host tooling and the LLM loop classify lines by `"addr"`, so conn
lines are picked up without changes. Connection data does NOT pass the Lua
hooks; generalizing the pipeline is the recorded future refactor trigger
(proposal §4 option C).

With `CONFIG_BLE_CONN_ENABLED=n`, `ble_conn.c` is excluded from the build,
the CONN commands answer `-451`, and firmware behavior is identical to the
feature-less product (off-build is the proof of no impact). Flash cost of
the feature: ~163 KB (mostly NimBLE central/GATT-client code; app 771 KB on
vs 604 KB off, 1.5 MB partition, 53%/62% free).

## 2. Public API (`ble_if.h`)

| Function | Description |
|----------|-------------|
| `ble_conn_init` | Create worker task, queues, mutex; register the scan tap. Idempotent. |
| `ble_conn_set_event_cb` | Connected/disconnected hook (wired to `power_hold_conn` in main.c — no ble→power dependency). |
| `ble_conn_set_target(svc, chr)` | Target service UUID (+ optional characteristic). Accepts 4-hex (16-bit), 8-hex (32-bit), canonical 36-char (128-bit). |
| `ble_conn_start(addr, type)` | NULL addr = auto-connect (async, watch status). With addr: direct connect, blocks until up/failed (bounded by the 6 s link timeout + EBUSY retries). |
| `ble_conn_stop` | Cancel search / abort connect / terminate link. |
| `ble_conn_get_state` / `ble_conn_state_name` / `ble_conn_is_active` / `ble_conn_get_status` | State + counters snapshot. |
| `ble_conn_set_poll_interval` | Poll fallback interval, 100..10000 ms (default 1000). |
| `ble_scan_set_tap(cb)` / `ble_scan_pause` / `ble_scan_resume` | F2.4 seams in the scan module (below). |

Error range `-450..-459`: -450 invalid param, -451 not compiled in (CLI),
-452 invalid state, -453 not connected, -454 discovery failed, -455
connect timeout, -456 no target configured.

## 3. Design (implemented)

- **State machine**: OFF → PEER_SEARCH → CONNECTING → DISCOVERING → ACTIVE.
  All state behind a module-owned mutex (nothing else takes it — review A6).
- **Concurrency** (review A2): NimBLE GAP/GATT callbacks run in the host
  task and only copy payloads into an 8-deep queue (drop-newest, counted).
  ONE worker task owns USB emission and the blocking connect sequence — a
  slow USB reader can never stall the BLE stack. All USB writes for conn
  lines happen in the worker, never in the host task.
- **Single GAP procedure** (NimBLE constraint): the scan's discovery is
  briefly paused around `ble_gap_connect` (`ble_scan_pause/resume`; the
  DISC_COMPLETE restart is suppressed while paused) and resumed when the
  link exists or the attempt fails — the user never loses the scan. The
  connect call retries on `BLE_HS_EBUSY` (5 × 30 ms).
- **Auto-connect**: while a user scan runs, a pre-dedup raw-report tap
  (`ble_scan_set_tap`) sees every advertisement and matches the target
  service UUID at ALL widths (16/32/128-bit — parsed in the tap via
  `ble_hs_adv_parse_fields`, because the proto layer surfaces only UUID16;
  review A4). Without a scan, ble_conn runs its own discovery and restarts
  it on window completion.
- **Direct connect address type** (review A3): explicit `public|random`
  argument > tap auto-learn (the type is remembered when the addressed peer
  is seen advertising) > public default.
- **GATT sequence**: MTU exchange (pin ATT_MTU 256) →
  `disc_svc_by_uuid` → `disc_all_chrs` (explicit target char, else
  auto-pick notify > indicate > readable) → `disc_all_dscs` for the 0x2902
  CCCD → CCCD write 0x0001/0x0002 → ACTIVE. No CCCD / no subscribe → poll
  fallback via `ble_gattc_read` from the worker loop (no extra timer task).
  Any discovery failure → terminate, `-454`.
- **Notifications**: `BLE_GAP_EVENT_NOTIFY_RX` filtered by the target value
  handle; payload capped at 253 B (MTU 256 − 3).
- **Power** (review D1): `power_hold_conn` OR-ed with `power_hold_activity`
  in the same `esp_pm` lock — stopping the scan never releases the
  connection's hold and vice versa; ~40 mA estimate while connected.

## 4. Line model (`json_encode_conn`, shared pure C)

- Payload is a JSON object → members merged after the envelope
  `{"ts":…,"addr":"…","src":"conn"…}`; payload keys `ts`/`addr`/`src` are
  dropped (envelope wins, review Q3). Member shapes are validated during
  the merge; any malformed member falls back to wrap mode.
- Other payloads → wrapped as `"data":"<escaped>"`.
- Overflow → wrap with `"trunc":true`; the data string is budget-truncated
  so the line is always valid JSON. Line budget 512 B (`JSON_LINE_MAX_LEN`).

## 5. CLI

```
CONN TARGET <svc-uuid> [<char-uuid>]
CONN START [<addr> [public|random]]
CONN STOP
CONN STATUS
CONN INTERVAL <ms>
```

State matrix (review A1): allowed in IDLE and SCANNING; in SCRIPT_RUNNING
only STOP and STATUS (conn lines bypass Lua hooks — running both would be
misleading), others → -911. Ctrl+C also disconnects. `STATUS` gains an
additive `"conn"` object; `CONN STATUS` carries the full counters.

## 6. Acceptance Criteria

| # | Criterion | Verified by |
|---|-----------|-------------|
| AC-1 | Feature-off build excludes ble_conn.c; CONN → -451; behavior unchanged | off-build + map (604 KB, no ble_conn) |
| AC-2 | Auto-connect searches by svc UUID (16/32/128-bit) via tap or own discovery | C0: search starts/stops; unit: UUID parser |
| AC-3 | Direct connect blocks, succeeds or fails (-455) bounded | C0: unreachable addr → -455, state off |
| AC-4 | Scan survives connect churn (pause/resume seam) | C0: coexistence section, adv stream intact |
| AC-5 | Line model: merge with envelope precedence, wrap, trunc:true, always valid JSON | host suite (22 cases) |
| AC-6 | State matrix incl. -911 in SCRIPT_RUNNING | host suite |
| AC-7 | Ctrl+C aborts search/connect and disconnects | C0 |
| AC-8 | Power hold OR-ed (scan stop does not drop conn hold) | design + POWER STATUS while connected |
| AC-9 | GATT path (subscribe/notify/poll/disconnect) | **OPEN — needs a peer** (C1-C6): second ESP32 or nRF Connect phone; WinRT GATT server unavailable on the current PC |

## 7. Open Items

1. **C1–C6 HIL** (real GATT peer): auto-connect + notify stream, direct
   connect, CONN STOP, peer-vanish, poll fallback, MTU >23 B payload.
   The pvvx/ATC temperature-sensor pilot (docs/usecase-pilot-*.md) is the
   intended first real peer and closes this gap.
2. Indication acks are treated like notifications (no ack wait) — fine for
   telemetry; revisit if a peer requires confirmed indications.
3. Binary payloads ≥0x80 pass through unvalidated UTF-8 in wrap mode
   (documented; hex mode rejected to keep lines small).

## 8. Improvement pass (2026-08-22, by the variant-A author, on this branch)

Following `docs/evaluation-ble-conn-zai-2026-08-22.md`, this branch
absorbed the best of variant A and fixed the nits found in B:

- Named error constants `BLE_CONN_ERR_*` in `ble_if.h` (incl. new
  `-457 INTERRUPTED`); all bare literals in `ble_conn.c` replaced.
- `os_mbuf_copydata` for notify/read payloads (chained-mbuf safe).
- Disconnect notices evict the oldest op-queue entry instead of dropping
  (A's notice semantics).
- `ble_att_set_preferred_mtu(256)` explicit at init.
- `ble_scan_resume()` failure now clears `s_scanning` (no zombie scan);
  `s_paused` is `atomic_bool`.
- Direct `CONN START` wait is sliced (100 ms) and honors Ctrl+C via new
  `usb_console_poll_interrupt()` (one-byte lookahead with pushback; the
  line stream stays intact). On interrupt the CLI runs the normal
  `h_interrupt` semantics and the connect is cancelled.
- C1–C6 WinRT GATT-server tier ported from A's `test_ble_conn_hw.py`
  into this branch's suite (skips where the API is unavailable).

Re-verified after the pass: host 108/108 · C0 29/29 · regressions
32/14/45/11 · on/off builds clean. AC-9 remains open (needs a real peer);
the ported C1–C6 tier runs it automatically wherever WinRT works.
