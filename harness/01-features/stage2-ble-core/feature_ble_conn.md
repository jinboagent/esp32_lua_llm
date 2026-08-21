# Feature: BLE Connection (GATT Client)

## Basic Info

| Field        | Value                                        |
|--------------|----------------------------------------------|
| Feature ID   | F2.4                                         |
| Stage        | 2 — BLE core (v2 increment, optional build)  |
| Layer        | L1 (driver/domain), per `coding_rules.md` §5 |
| Dependencies | F2.1 (nimble init), F0.1 (usb_cdc), F4.3 (power) |
| Source Files | `firmware/components/ble/ble_conn.c`, `firmware/components/ble/Kconfig.projbuild` |
| Test Files   | `tests/host/test_ble_conn.c` (Unity) + root `test_ble_conn_hw.py` (HIL) |
| Provenance   | Promoted from `docs/feature-proposal-ble-conn-2026-08-16.md` after two independent reviews (`docs/review-ble-conn-2026-08-16-zai.md`, `-deepseek.md`); amendments A1–A6/D1–D7 absorbed below |

## Functional Description

Central-role connection to one peer: discover or directly connect,
discover a target GATT service/characteristic, subscribe to
notifications/indications (fallback: periodic reads), and re-stream the
payloads as JSON lines on the same USB stream as adv lines. Gated by
`CONFIG_BLE_CONN_ENABLED` (default y; `select BT_NIMBLE_ROLE_CENTRAL`);
the off-build behaves identically to v1.0.0.

### State machine

```
 IDLE ─CONN START─▶ PEER_SEARCH ─match─▶ CONNECTING ─▶ DISCOVERING ─▶ ACTIVE
  ▲                     │ (direct addr: skip search)        │            │
  └──CONN STOP / disconnect / error / Ctrl+C────────────────┴────────────┘
```

### CLI grammar (family `CONN`)

```
CONN TARGET <svc-uuid> [<char-uuid>]     16/32/128-bit forms (ble_uuid_init_from_str)
CONN START [<addr> [public|random]]      no addr = auto-connect by target svc UUID
CONN STOP                                disconnect
CONN STATUS                              state, addr, addr_type, mode, counters
CONN INTERVAL <ms>                       poll fallback interval (100..10000, default 1000)
```

**A1 state matrix** (CLI state is derived, no new enum value):
`CONN START/TARGET/INTERVAL` allowed in IDLE and SCANNING, rejected in
SCRIPT_RUNNING with `-911`. `CONN STOP` and `CONN STATUS` always allowed.
Ctrl+C stops upload + script + scan **and** disconnects (stop-everything).

### Data path (A2 — never block the radio)

Notifications/read results arrive in the NimBLE host task. The GATT
callback only **enqueues** (addr + ts + payload copy) into a static queue
(depth 8, drop-newest, `drops` counter). A dedicated emitter context
(the conn task) dequeues and calls `json_encode_conn()` →
`usb_console_send_json()`. Nothing in the host task touches the USB TX
mutex (which has a 100 ms take timeout).

### Line model (D2 — envelope wins)

- Payload is a JSON object **and** has no top-level `ts`/`addr`/`src`
  key → merged: `{"ts":T,"addr":"A","src":"conn",<payload fields>}`.
- Collision or non-object → wrapped: `{"ts":T,"addr":"A","src":"conn","data":"<escaped>"}`.
- Truncation (A5): encoded line must stay ≤ `JSON_LINE_MAX_LEN` (512)
  and valid JSON; on overflow emit wrapped form with `"trunc":true`.

### Sizing / MTU (A5)

- `ble_att_set_preferred_mtu(256)` at init + explicit
  `ble_gattc_exchange_mtu()` on connect (pinned; not left to defaults).
- Max single-notification payload = MTU−3 = 253 B (`BLE_CONN_PAYLOAD_MAX_LEN` 256 buffer).
- **No reassembly**: one notification = one emitted line.

### Targeting (A3, A4)

- Direct: optional `public|random` arg (default `public`); when omitted
  the type is auto-learned from an 8-entry addr→type cache fed by the
  scan tap; failure mode with a wrong guess is a connect timeout
  (`-455`) — documented in README.
- Auto: `ble_scan` exposes a raw-report tap (`ble_scan_set_tap`, no-op
  when unset, called for every DISC event before dedup). While
  PEER_SEARCH the tap parses raw adv data itself via
  `ble_hs_adv_parse_fields()` (AD 0x02/0x03, 0x04/0x05, 0x06/0x07 →
  uuid16/32/128 matching). When no scan is active, conn runs its own
  `ble_gap_disc` (NimBLE allows one discovery at a time) with the same
  parse. **Limitation (passive scan):** UUIDs present only in scan
  responses are invisible; active scanning during peer search is a
  deferred product decision (config-gated, separate review).

### Concurrency & init (A6)

- `ble_conn` owns one mutex; touched from the NimBLE host task (GAP/GATT
  callbacks) and the CLI task; no other module takes it.
- `ble_conn_init()` called from `app_main` under
  `#ifdef CONFIG_BLE_CONN_ENABLED` (explicit boot-chain seam).

### Power (gap #1 + D1)

- `power_hold_conn(bool)` in `power_if.h` OR-ed with the scan hold
  (single esp_pm lock, two holder booleans).
- `ble_conn` never includes `power_if` (component cycle: power already
  REQUIRES ble). Wiring: `ble_conn_set_event_cb()` — `main.c` registers
  a callback that calls `power_hold_conn` on CONNECTED/DISCONNECTED.
- Observability: `power_get_state()`/`power_get_current_ma()` treat
  `ble_conn_is_active()` as ACTIVE / 40 mA (`POWER_MA_CONNECTED`).

### Security posture

No bonding ("just works"). Peers requiring encrypted/paired access
return "insufficient authentication" — unsupported by design in this
version; `CONFIG_BT_NIMBLE_SECURITY_ENABLE` already present makes a
pairing follow-up additive.

## I/O Definitions

### Error codes (module range -450…-459)

| Code | Meaning |
|------|---------|
| -450 | invalid parameter (bad UUID/addr string, interval range) |
| -451 | feature not compiled in (`CONFIG_BLE_CONN_ENABLED=n`) |
| -452 | invalid state (e.g. START while connected, TARGET while ACTIVE) |
| -453 | not connected (STOP while idle) |
| -454 | discovery failed (service/characteristic not found) |
| -455 | connect timeout / peer unreachable |
| -456 | no target configured (auto START without TARGET) |

### Public API (appended to `interfaces/ble_if.h`)

```c
int  ble_conn_init(void);
int  ble_conn_set_target(const char *svc_uuid, const char *char_uuid); /* char may be NULL */
int  ble_conn_start(const char *addr, int addr_type); /* addr NULL = auto; addr_type -1 = learn/public, 0 public, 1 random */
int  ble_conn_stop(void);
bool ble_conn_is_active(void);
int  ble_conn_set_poll_interval(uint32_t ms);
int  ble_conn_get_info(ble_conn_info_t *info);
typedef void (*ble_conn_event_cb_t)(int event); /* 1=connected 2=disconnected */
int  ble_conn_set_event_cb(ble_conn_event_cb_t cb);
```

`ble_conn_info_t`: state, addr[6], addr_type, mode (notify|poll|none),
rx_lines, drops, poll_ms, connected bool.

### Additions to neighbours

- `ble_scan.c`: `void ble_scan_set_tap(void (*cb)(const adv_report_raw_t *))`.
- `json_if.h`/`json_encoder.c`: `int json_encode_conn(uint32_t ts_ms, const char *addr, const char *payload, char *buf, uint16_t buf_len, uint16_t *out_len)` — shared pure implementation (both builds).
- `power_if.h`/`power_mgmt.c`: `power_hold_conn(bool)`; observability per §Power.
- `sdkconfig.defaults`: `CONFIG_BLE_CONN_ENABLED=y`, `CONFIG_BT_NIMBLE_MAX_CONNECTIONS=1` (deliberate; was IDF default 3).

## Acceptance Criteria

1. Off-build (`=n`) compiles and behaves byte-identically to v1.0.0 (existing suites green; `CONN` → `-451`).
2. `CONN TARGET` + `CONN START` (auto) connects to a peer advertising the svc UUID while scan runs (tap path) and while idle (own-disc path).
3. `CONN START <addr> random` connects to a random-static peer; omitted type auto-learned from tap when seen.
4. Notify peer: payloads appear as `src:"conn"` lines; merged form for clean objects; wrapped form on collision/non-object; `trunc:true` on overflow; all lines valid JSON ≤ 512 B.
5. Poll-fallback peer (read-only characteristic): lines at `CONN INTERVAL`.
6. Chatty peer (50 Hz): no host-task stall (scan drop counter unchanged vs baseline), `drops` > 0 surfaced in `CONN STATUS`, device responsive.
7. `CONN STOP`, peer vanish, and Ctrl+C all return to IDLE, release the power hold, emit a disconnect notice.
8. `STATUS` shows `"conn":{enabled,connected,addr,...}`; `POWER STATUS` reports active/40 mA while connected with scan off (D1).
9. START/TARGET/INTERVAL rejected in SCRIPT_RUNNING with -911; STOP/STATUS always work (A1).
10. `ble_conn` mutex is the only lock it takes; pipeline/CLI never acquire it.
11. No malloc/free in ble_conn (static queue, static buffers).
12. Host suite grows (encoder precedence/truncation/merge) and stays green; HIL C1–C6 green or gracefully skipped.

## Test Cases

| ID | Layer | Scenario | Expected |
|----|-------|----------|----------|
| TC-1 | host | merge clean object | envelope + payload fields, envelope first |
| TC-2 | host | payload with top-level `addr` | wrapped form, envelope wins |
| TC-3 | host | non-object payload | wrapped `"data"` escaped |
| TC-4 | host | 300 B payload | valid JSON ≤ 512, `trunc:true` |
| TC-5 | host | control chars in payload | escaped, valid JSON |
| TC-6 | host | tiny buffer | -203 |
| TC-7 | HIL C1 | auto-connect by UUID (scan active) | conn lines flow |
| TC-8 | HIL C2 | direct connect addr+random | connected |
| TC-9 | HIL C3 | CONN STOP | IDLE, notice line |
| TC-10 | HIL C4 | peer vanishes | disconnect notice, IDLE |
| TC-11 | HIL C5 | 50 Hz notify 2 s | drops counted, STATUS ok |
| TC-12 | HIL C6 | POWER STATUS mid-conn, scan off | active, 40 mA |
| TC-13 | build | `=n` build | suites green, CONN → -451 |

## Non-Functional Constraints

| Constraint | Requirement |
|------------|-------------|
| Memory | static queue 8×(256+meta); zero heap |
| Radio | host-task callback enqueue-only (<1 ms) |
| Layering | L1; no power_if include (cycle avoidance) |
| Stack | C11, NimBLE central, ESP-IDF v5.1 |

## Implementation Notes (v2.0, 2026-08-21)

- Reviews absorbed: A1 state matrix; A2 queue+emitter; A3 addr-type arg +
  tap auto-learn; A4 tap-side 128-bit parse + passive-scan limitation;
  A5 MTU exchange + no-reassembly + trunc marker; A6 own mutex + explicit
  init; D1 power observability; D2 envelope precedence.
- Deferred (documented, not dropped): active scan during peer search;
  bonding; Lua-over-conn (Option-C refactor trigger); soak-script conn
  cycle (needs a PC GATT peer during soak).
- `LUA_SOURCE` is a conditional-*dependency* switch; the conditional
  *sources* mechanism used here is plain CMake `if(CONFIG_…) list(APPEND …)`.
- `test_ble_peer_hw.py` P3 boundary comment updated: central role exists
  but the device still neither advertises nor accepts connections.

### Verification results (2026-08-22)

- Host suite 95/95 (86 prior + 9 conn-encoder: precedence, wrap, trunc,
  escaping, limits). Two encoder bugs caught by these tests before flash:
  raw-control-char payloads leaking into merged lines, and a 13-vs-12
  char `,"trunc":true` budget off-by-one.
- Firmware build clean (`-Wall -Wextra -Werror=all`); off-build
  (`CONFIG_BLE_CONN_ENABLED=n`) compiles with `ble_conn.c` excluded
  (0 references in build log); on-build restored after.
- HIL `test_ble_conn_hw.py` on device: C0 control plane 14/14
  (validation, tap-path + own-discovery search start/stop, -450/-453).
  C1-C6 (GATT data path) SKIP on this machine: the installed WinRT
  binding cannot create a local GATT server (`create_async` ->
  E_ILLEGAL_METHOD_CALL from a non-interactive context) and lacks
  `is_connectable` on the advertiser - environment limitation, not a
  firmware fault. Data path remains covered by host encoder tests +
  the manual runbook below.
- Regression: bridge 32/32, power 14/14, ble_lua 45/45, peer 11/11.

### Manual runbook (GATT data path, when no WinRT GATT server)

Phone with nRF Connect: create a GATT server with service
`12345678-1234-1234-1234-123456789abc`, characteristic
`12345678-1234-1234-1234-123456789a01` (NOTIFY+READ), start
advertising; then on the dongle:
`CONN TARGET 12345678-1234-1234-1234-123456789abc
12345678-1234-1234-1234-123456789a01`, `CONN START`; expect
`"src":"conn"` lines mirroring the notifications.
