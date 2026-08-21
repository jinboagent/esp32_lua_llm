# Feature Proposal — BLE Connection (GATT Client) as an Optional Product Feature

| Field       | Value                                              |
|-------------|----------------------------------------------------|
| Date        | 2026-08-16                                         |
| Branch      | `ble_connected`                                    |
| Status      | **PROPOSAL — awaiting review**                     |
| Author      | Qwen Code session (with product owner decisions)   |
| Scope       | Review document only; no code changes yet          |

**What reviewers are asked to evaluate:** (1) the feature requirement
itself (§2), (2) the patch-vs-redesign reasoning (§3–§4), (3) the impact
on the shipped v1.0.0 firmware (§5), (4) the verification plan (§6).
After approval, this document is promoted to a harness feature spec and
implemented in a follow-up session (§8).

---

## 1. Background & Motivation

The shipped product (v1.0.0) is a **passive** BLE sniffer: it observes
advertisements, parses them, filters them (C engine + Lua sandbox), and
streams JSON lines over USB CDC to a host PC, where an LLM closes the
product loop by generating Lua filters (`llm_loop.py`, stage5-host).

Advertisements carry at most 31 bytes. Many real devices expose their
interesting data (sensor readings, device state, JSON payloads) only
over a **GATT characteristic** after a connection is established. Today
the dongle cannot read any of it. This proposal adds a **central-role
connection** capability: connect to one peer, read its JSON (via
notifications or reads), and re-stream it on the same USB JSON line
stream — so the existing host tooling and LLM loop see connection data
with zero changes.

The original design anticipated this: `project_overview.md` lists
"BLE connections (GATT client/server)" under *Out of scope for v1*, and
`harness/02-future/README.md` carries it in the v2 candidate table.
This proposal is that parked requirement being pulled forward.

## 2. Feature Requirement

### 2.1 User stories

- As an IoT developer, I want the dongle to connect to my device by its
  advertised **service UUID** and stream its JSON payload to my PC, so
  I can debug connected data the same way I debug advertisements.
- As an IoT developer, I want to connect by **address** when I already
  know which device I care about.
- As a product owner, I want builds **with and without** this feature,
  so minimal sniffer builds stay minimal (flash/RAM/attack surface).

### 2.2 Product-owner decisions (confirmed 2026-08-16)

| Decision      | Choice                                              |
|---------------|-----------------------------------------------------|
| Targeting     | Both: `CONN START <addr>` direct, and `CONN START` auto-connect to the first device advertising the configured service UUID |
| Data path     | Subscribe to notifications/indications; fall back to periodic reads |
| Configurability | Kconfig build flag **and** runtime start/stop      |
| UUIDs         | CLI-configurable per session, with built-in defaults |

### 2.3 Proposed defaults (reviewable)

- **Max 1 connection** (the dongle observes one peer of interest).
- **No bonding** — LE "just works" association, no MITM protection.
  Rationale: sniffer/debug product reading non-sensitive telemetry;
  bonding adds NVS key management and pairing UX scope. Reviewers may
  require bonding; it would be an additive follow-up.
- **Notify preferred, indicate second, poll last.**
- **Poll interval** default 1000 ms, range 100–10000 ms.
- Connection **coexists with scanning** (NimBLE schedules both roles).
- Connection data **does not pass Lua hooks** (`on_adv`/`transform`)
  this version — see §4 option C for the recorded refactor trigger.

### 2.4 CLI grammar (new command family `CONN`)

```
CONN TARGET <svc-uuid> [<char-uuid>]   set target UUIDs (16/32/128-bit forms)
CONN START [<addr>]                    connect (auto by svc-uuid when no addr)
CONN STOP                              disconnect
CONN STATUS                            state, peer addr, rssi, mode, counters
CONN INTERVAL <ms>                     poll fallback interval (100..10000)
```

Responses follow the existing JSON contract (`{"status":"ok|error",
"cmd":"conn_…", …}`). Error codes use a new module range **-450…-459**:
-450 invalid param (bad UUID/addr), -451 not compiled in, -452 invalid
state, -453 not connected, -454 discovery failed, -455 connect timeout,
-456 no target configured.

### 2.5 Connection data line model

Emitted on the **same** USB JSON stream as adv lines (host tools and
`llm_loop.py` already classify lines by `"addr"` — they pick conn lines
up for free):

- Payload is a JSON object → fields merged:
  `{"ts":<ms>,"addr":"AA:BB:CC:DD:EE:FF","src":"conn",<payload fields>}`
- Otherwise → wrapped:
  `{"ts":<ms>,"addr":"…","src":"conn","data":"<escaped payload>"}`

`STATUS` gains a nested `"conn":{enabled,connected,addr,mode}` object
(additive; existing host consumers ignore unknown fields).

### 2.6 Data Flow (as-is vs proposed)

**As-is (v1.0.0) — adv path only** (mirrors `project_overview.md` §Data Flow):

```
BLE radio → NimBLE scan callback → raw ADV → FreeRTOS queue
  → AD parser → filter engine (C rules + optional Lua on_adv/transform)
  → json_encode_adv → usb_console_send_json → USB CDC → host
```

**Proposed — conn path added; the adv path above is byte-for-byte
unchanged:**

```
                    CONTROL PLANE (existing CLI + new family)
          CONN TARGET/START/STOP/STATUS/INTERVAL, Ctrl+C
                               │
                               v
 +----------+  connect   +-----------+  GATT disc   +------------+
 |  BLE     |===========>| ble_conn  |=============>| svc + char |
 |  radio   |<===========| (central) |<=============| discovery  |
 +----------+  notify /  +-----------+  read        +------------+
              poll payload      │
          (adv path still       │
           flows in parallel)   v
                     json_encode_conn
                  (merge JSON object,
                   else wrap "data")
                                │
                                v
                    usb_console_send_json   ← SAME stream as adv lines
                                │
                                v
                      USB CDC → host (llm_loop / LLM see both kinds)
```

**Auto-connect peer search** (NimBLE runs one discovery at a time):

```
scan active? ──yes──> ble_scan raw-report tap ─> match svc UUID ─┐
      │                                                          ├─> ble_gap_connect
      └──no───> ble_conn own ble_gap_disc ─────> match svc UUID ─┘
```

**Side channels:**

- `power_hold_conn(true)` on connect / `false` on disconnect, OR-ed with
  the scan hold so neither releases the other's light-sleep block.
- Disconnect events (peer vanish, `CONN STOP`, Ctrl+C) return the state
  machine to IDLE and emit a `{"status":"ok","cmd":"conn_stop"…}` /
  disconnect notice line.
- Deliberately **not** in the flow: Lua hooks (decision §2.3) and
  pipeline stats (stay adv-only; conn counters live in `CONN STATUS`).

## 3. As-Is Seam Analysis (what the v1 design did / didn't anticipate)

### Anticipated — the feature plugs into existing contracts

| Seam | Evidence | Reused by this feature |
|------|----------|------------------------|
| Requirement parked | `project_overview.md` "Out of scope for v1"; `harness/02-future/README.md` v2 table | requirement traceability |
| Component boundary | `firmware/components/ble/` (init/scan/pipeline) | new `ble_conn.c` joins without touching scan |
| Interface headers | `interfaces/ble_if.h` etc. | conn API appended to `ble_if.h` |
| Output contract | `usb_console_send_json()` (scan_pipeline.c) | conn lines use the same stream |
| JSON escaping | `json_escape_str()` (`interfaces/json_if.h`, H1 fix) | safe payload embedding |
| CLI dispatch | strcmp chain + `s_is_cli_command` + `h_interrupt` (cli_commands.c) | one guarded family added |
| Stack role flag | `CONFIG_BT_NIMBLE_ROLE_CENTRAL=n` (sdkconfig.defaults) | one config flip enables central |
| Conditional compile | `LUA_SOURCE` CMake cache var (ble/CMakeLists.txt) | precedent for feature-gated sources |

### Not anticipated — four contained gaps

1. **`power_hold_activity(bool)`** is a single lock documented as
   "hold on SCAN START, release on SCAN STOP" — scan is the only
   assumed holder. A connection is a second holder; stopping one must
   not release the other. Fix: add `power_hold_conn(bool)` OR-ed into
   the same policy (additive API, host-tested).
2. **No product feature-flag precedent** — no `Kconfig.projbuild`
   exists; this introduces the first, establishing the convention
   (`BLE_CONN_ENABLED`, `select BT_NIMBLE_ROLE_CENTRAL`).
3. **STATUS / boot banner / Ctrl+C assume streaming = scan.** Each gets
   a small `#ifdef`-guarded addition (conn field; banner line;
   `h_interrupt` also disconnects).
4. **HIL peer advertises only** (`test_ble_peer_hw.py` comment:
   connections are v2). Windows WinRT *can* serve GATT
   (`GattServiceProvider`), so a controlled-peer HIL suite is feasible;
   it degrades to `skip()` on adapters without server support.

### Known NimBLE constraint discovered during design

The host runs **one discovery procedure at a time**. Auto-connect's
"find my service UUID" therefore cannot run its own `ble_gap_disc`
while the user's SCAN is active (`BLE_HS_EBUSY`). Design: `ble_scan`
gains an optional **raw-report tap** (`ble_scan_set_tap(cb)`, additive,
no-op when unset); auto-connect uses the tap when scanning is active,
else its own discovery. This keeps "sniff and connect simultaneously"
working without touching scan logic beyond the tap.

## 4. Design Options & Trade-offs

### Option A — Additive `ble_conn.c` + first Kconfig flag (RECOMMENDED)

New state machine file inside the existing `ble` component; CMake
appends it only under `CONFIG_BLE_CONN_ENABLED`; CLI/STATUS/interrupt
additions `#ifdef`-guarded; new error range; new tests. Old-code diffs
are small, at known extension points, and the feature-off build behaves
identically to today's firmware — **the off-build is the proof of "no
impact"**.

### Option B — Separate `ble_conn` component

Stricter isolation (own directory/CMake), but duplicates the component
plumbing (REQUIRES lists, interface wiring) and splits one BLE domain
across two components. Chosen only if reviewers want physical
separation; functionally identical to A.

### Option C — Redesign the pipeline to generic "line sources" now

Generalize `scan_pipeline` (adv-shaped today: `on_adv(addr, addr_type,
rssi, name, uuids, manu_id, manu_data)`) into a source-agnostic
pipeline so adv and conn data both flow through filters and Lua hooks.
Long-term cleanest, but rewrites shipped, tested code for a need that
does not exist yet (no requirement asks for Lua-over-conn). **Rejected
for now; recorded as the refactor trigger**: when a second line source
needs Lua processing, generalize the pipeline then. This is the
patch-vs-redesign rule applied: *patch when the feature fits the seams;
redraw a boundary only when the feature proves the boundary wrong.*

## 5. Impact Analysis

| File | Kind | Change | Risk | Mitigation |
|------|------|--------|------|------------|
| `firmware/components/ble/ble_conn.c` | new | central state machine (IDLE→PEER_SEARCH→CONNECTING→DISCOVERING→ACTIVE), subscribe/poll, line emit | med (new radio activity) | flag-gated; HIL; regression suites |
| `interfaces/ble_if.h` | additive | conn API + -450…-459 | low | compile-time |
| `firmware/components/ble/ble_scan.c` | additive | raw-report tap (`ble_scan_set_tap`) | low | no-op default; existing suites |
| `firmware/components/ble/CMakeLists.txt` | modif | conditional SRCS append | low | `LUA_SOURCE` pattern |
| `firmware/components/ble/Kconfig.projbuild` | new | `BLE_CONN_ENABLED` (select CENTRAL) | low | menuconfig + off-build check |
| `sdkconfig.defaults` | additive | 2 lines | low | — |
| `firmware/components/cli/cli_commands.c` | modif (guarded) | `h_conn`, dispatch, `s_is_cli_command`, STATUS conn field, interrupt, banner | med (shared file) | `#ifdef`; host CLI tests; HIL regression |
| `firmware/components/json_enc/*` | additive | `json_encode_conn()` (pure) | low | host unit tests |
| `firmware/components/power/*` | additive | `power_hold_conn()` OR-ed hold | med (power policy) | host power tests; soak observes no sleep mid-conn |
| `tests/host/test_ble_conn.c` (+main/CMake) | new | encoder + CLI parse cases | low | — |
| `test_ble_conn_hw.py` | new | WinRT GATT-server peer HIL | med (adapter variance) | graceful `skip()`; manual fallback (nRF Connect) |
| `README.md` | additive | commands, highlights, test row | low | — |

## 6. Test & Verification Plan

1. **Host (Unity, MinGW)**: `json_encode_conn` merge / escape /
   truncation-to-valid-JSON cases; CONN argument parsing via existing
   CLI host stubs. Suite grows from 86.
2. **HIL `test_ble_conn_hw.py`** (PC = WinRT GATT server, notifying
   JSON once/s): C1 auto-connect by service UUID + conn lines flow;
   C2 direct connect by address; C3 `CONN STOP` clean disconnect;
   C4 peer vanishes → disconnect status, back to idle; C5 poll-fallback
   peer (read-only characteristic).
3. **Regression**: host suite; `test_bridge_hw.py`, `test_power_hw.py`,
   `test_ble_lua_hw.py`, `test_ble_peer_hw.py` all green on new firmware.
4. **Feature-off build**: `CONFIG_BLE_CONN_ENABLED=n` compiles; `CONN`
   → `-451 not enabled`; boot/scan behavior unchanged; map size delta
   recorded.
5. **Manual runbook**: PuTTY smoke — `CONN TARGET …`, `CONN START`,
   watch lines, Ctrl+C recovers, `CONN STOP`.
6. **Runtime health**: `STATUS` `free_heap` with conn+scan concurrent
   (RAM headroom for the central role).

## 7. Open Questions for Reviewers

1. Is no-bonding "just works" acceptable for v2.0 telemetry use?
2. Should conn lines get their own counters in `STATUS` (proposed:
   yes, inside the `conn` object; pipeline stats stay adv-only)?
3. Is the `src:"conn"` merged-line model acceptable to downstream host
   tooling, or should conn data be namespaced under a `data` object?
4. Auto-connect while scanning: is the raw-tap approach acceptable, or
   should auto-connect require SCAN STOP (simpler, worse UX)?
5. Any requirement foreseen for Lua processing of conn data (would
   trigger option-C refactor)?

## 8. Post-Review Path

1. Reviewer sign-off (or amendments) on this document.
2. Promote to `harness/01-features/stage2-ble-core/feature_ble_conn.md`
   (F2.4); remove from `harness/02-future/README.md` candidate table.
3. Mirror §2.6 into `project_overview.md` (Architecture box diagram +
   Data Flow section gain the conn path) — project docs update only
   after review, per the review-first agreement.
4. Implement per §5 order: spec → headers → impl → host tests → HIL →
   docs, on branch `ble_connected`.
5. Status report + LATEST pointer; commit with the repo's 4-section
   commit-message standard.
