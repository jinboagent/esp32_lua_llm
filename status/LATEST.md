# LATEST — Status Pointer

**Current status:** [`status-2026-08-22-improvement-pass.md`](status-2026-08-22-improvement-pass.md)

## At a glance

- **Master is the F2.4 product line** (adoption decided 2026-08-23): the
  improved variant (zai) was merged forward and pushed; `ble_connected`
  preserves variant A as the reference branch
- **Improvement pass** (2026-08-22): evaluation
  (`docs/evaluation-ble-conn-zai-2026-08-22.md`) became a merge — named
  error constants (+`-457`), `os_mbuf_copydata`, notice-evicts-oldest,
  preferred-MTU pin, zombie-scan + atomic-paused fixes, Ctrl+C-aware
  sliced direct-start wait (`usb_console_poll_interrupt`), C1–C6 WinRT
  GATT tier ported
- **README deep-dive cherry-picked to master** (2026-08-26): architecture
  subsections (layers/threads/contracts/memory/flags), content map,
  guided usage tour + troubleshooting, WSL host-test fallback — facts
  adapted to the improved variant (worker prio 2, 108/108, C0 29)
- **Verified post-pass**: host 108/108 · C0 29/29 · regressions
  32/14/45/11 · on/off builds clean
- **Open**: AC-9 real-peer GATT data path (ATC sensor pilot / nRF
  Connect); push `ble_connected` reference branch when permitted;
  re-soak stays deferred
