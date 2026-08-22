# Status — 2026-08-22 (improvement pass on `ble_connected_zai`)

## What
The variant-A author evaluated zai's F2.4 implementation
(`docs/evaluation-ble-conn-zai-2026-08-22.md`), then — per owner
instruction — applied the recommended merge **to this branch** and left
`ble_connected` untouched.

## Changes
- From A: named `BLE_CONN_ERR_*` constants (+ `-457 INTERRUPTED`),
  `os_mbuf_copydata` (notify+read), notice-evicts-oldest, explicit
  `ble_att_set_preferred_mtu(256)`, C1–C6 WinRT GATT-server HIL tier.
- B nits: `ble_scan_resume()` failure clears `s_scanning`; `s_paused`
  atomic.
- New: `usb_console_poll_interrupt()` (pushback-safe 0x03 lookahead);
  direct `CONN START` wait sliced 100 ms and Ctrl+C-aware; on interrupt
  the CLI runs `h_interrupt` and returns the interrupt response.
- Docs: evaluation + comparison addenda, spec §8, README test rows,
  gitignore for zai scratch.

## Verification
- Host Unity 108/108 (`-Wall -Wextra -Werror`, WSL gcc)
- Firmware on-build clean; off-build (`=n`) clean, 0 `ble_conn.c` refs
- Flashed: C0 29/29 (incl. `-455` sync timeout, Ctrl+C mid-search)
- Regressions: bridge 32 · power 14 · ble_lua 45 · peer 11 — green
- C1–C6 skip on this PC (WinRT GATT server unavailable; unchanged)

## Open
- AC-9 real-peer GATT data path (ATC sensor pilot / nRF Connect)
- Push both branches when the owner permits
