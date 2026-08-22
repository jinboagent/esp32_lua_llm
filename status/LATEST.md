# LATEST — Status Pointer

**Current status:** [`status-2026-08-22-improvement-pass.md`](status-2026-08-22-improvement-pass.md)

## At a glance

- **Improvement pass landed on `ble_connected_zai`** (2026-08-22): the
  variant-A author's evaluation (`docs/evaluation-ble-conn-zai-2026-08-22.md`)
  became a merge — named error constants (+`-457`), `os_mbuf_copydata`,
  notice-evicts-oldest, preferred-MTU pin, zombie-scan + atomic-paused
  fixes, Ctrl+C-aware sliced direct-start wait (`usb_console_poll_interrupt`),
  C1–C6 WinRT GATT tier ported. `ble_connected` untouched
- **F2.4 base implementation** (zai): see
  [`status-2026-08-22-zai-conn.md`](status-2026-08-22-zai-conn.md)
- **Verified post-pass**: host 108/108 · C0 29/29 · regressions
  32/14/45/11 · on/off builds clean
- **Open**: AC-9 real-peer GATT data path (ATC sensor pilot); push both
  branches when permitted
