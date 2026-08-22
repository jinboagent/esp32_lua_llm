# LATEST — Status Pointer

**Current status:** [`status-2026-08-22-zai-conn.md`](status-2026-08-22-zai-conn.md)

## At a glance

- **F2.4 independently implemented on `ble_connected_zai`** (this branch):
  optional BLE connection (GATT client) — CONN command family, auto-connect
  by service UUID (tap, all UUID widths) or direct address, notify/indicate
  + poll fallback, `"src":"conn"` line model with envelope precedence
- **Spec promoted**: `harness/01-features/stage2-ble-core/feature_ble_conn.md`;
  build flag `CONFIG_BLE_CONN_ENABLED` (off-build proof: 604 KB, no
  `ble_conn.c`, CONN → -451)
- **Verified**: host **108/108** (22 new) · C0 peerless HIL **29/29** ·
  regressions bridge 32 / power 14 / ble_lua 45 / peer 11 all green ·
  on/off builds clean (feature ≈ +163 KB)
- **Open**: GATT data path C1–C6 needs a real peer (ATC sensor pilot);
  comparison vs the sibling implementation on `ble_connected` in progress
- **Base**: cut from `b9053db` (proposal commit) — same starting point as
  `ble_connected`, for a like-for-like code comparison
