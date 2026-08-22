# Status — 2026-08-22 (zai): F2.4 independent implementation on `ble_connected_zai`

## What

Independent implementation of the F2.4 BLE connection (GATT client) feature
on branch `ble_connected_zai`, cut from `b9053db` (the proposal commit —
the same starting point the `ble_connected` agent had) so the two
implementations can be compared side by side. The other implementation was
NOT read during development; comparison is a separate step.

## Implementation shape (design decisions of this variant)

- `ble_conn.c` (~850 lines): OFF/PEER_SEARCH/CONNECTING/DISCOVERING/ACTIVE;
  own mutex (A6); worker task owns ALL USB emission + the blocking connect
  with EBUSY retries (A2); op queue + 8-deep payload queue, drop-newest.
- Scan seams in `ble_scan.c`: pre-dedup raw-report tap (A4, matches
  16/32/128-bit service UUIDs via `ble_hs_adv_parse_fields`) and
  pause/resume of discovery around `ble_gap_connect` (single GAP procedure
  slot), so the user scan always survives connect churn.
- `json_encode_conn` in `json_encoder.c`: one pure-C implementation shared
  by ESP32 and host builds — merge with envelope precedence (ts/addr/src
  win, review Q3), wrap with escaping, `trunc:true` budget-truncation that
  always closes the line (two host-test bugs caught and fixed here).
- Direct connect: `public|random` arg > tap auto-learn > public (A3);
  blocks bounded by the 6 s link timeout; poll fallback runs from the
  worker loop (no extra timer). MTU exchange pins 256 (A5).
- Power: `power_hold_conn` OR-ed with `power_hold_activity` in one
  `esp_pm` lock (A8); 40 mA estimate while connected; wired via event cb
  in `main.c` (no ble→power dependency).
- Kconfig `BLE_CONN_ENABLED` (default y, selects `BT_NIMBLE_ROLE_CENTRAL`)
  + CMake source gating; CONN family answers -451 in off-builds (A1 state
  matrix: -911 in SCRIPT_RUNNING except STOP/STATUS).

## Verification

- Host Unity **108/108** (86 + 22 new: encoder merge/wrap/trunc/escapes +
  CONN command family + state matrix + STATUS conn object) — MinGW,
  `-Wall -Wextra -Werror`
- Firmware build ON: clean, app 0xb61d0 (771 KB, 53% free); OFF build:
  clean, app 0x938b0 (604 KB, no `ble_conn.c`, CONN → -451 by design)
  → feature cost ~163 KB (NimBLE central machinery)
- Flashed COM12: **C0 peerless control plane 29/29** (boot object, error
  paths -450/-453/-456, peer search start/stop/Ctrl+C, unreachable direct
  connect → -455 in ~6 s, scan coexistence, adv stream intact)
- Regressions: bridge **32/32** · power **14/14** · ble_lua **45/45** ·
  peer **11/11**
- **Open**: GATT data path C1–C6 needs a real peer (WinRT GATT server
  unavailable on this PC) — the pvvx/ATC sensor pilot is the intended
  first real peer.

## Files

New: `ble_conn.c`, `ble/Kconfig.projbuild`, `tests/host/test_ble_conn.c`,
`test_ble_conn_hw.py`, `harness/01-features/stage2-ble-core/feature_ble_conn.md`.
Modified: `ble_if.h`, `json_if.h`, `power_if.h`, `json_encoder.c`,
`ble_scan.c`, `power_mgmt.c`, `cli_commands.c`, `main/main.c`,
`ble/CMakeLists.txt`, `sdkconfig.defaults`, `sdkconfig`, stubs/host CMake,
`README.md`, `harness/02-future/README.md`, `test_ble_peer_hw.py` (P3 note).

## Next

- Independent comparison vs the `ble_connected` implementation →
  `docs/compare-ble-conn-*.md` (for the product owner's own side-by-side).
- C1–C6 with a real peripheral; optional re-soak with conn active.
