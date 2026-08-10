# Bug Fix Report — Manual BLE Scan Test Findings N1/N2/N3 (2026-08-10)

## Source

Not from an evaluation doc — found during a **manual BLE scan test** on
COM12 (user-requested hardware test, firmware `2fa1000`). The automated
suites never caught these because they only observe the first seconds
after `SCAN START`.

## Summary Table

| ID | Severity | Disposition | Fix |
|----|:--------:|-------------|-----|
| N1 | 🟠 | Fixed | Reception timestamps: `ts_ms` set in the GAP handler, propagated raw→parsed→JSON |
| N2 | 🟠 | Fixed (final: `BLE_HS_FOREVER`) | Continuous discovery; DISC_COMPLETE restart kept as fallback |
| N3 | 🟡 | Characterized — chip behavior, not fixable in firmware | `reset_reason` added to boot banner + STATUS for field observability |

## N1 — `"ts"` always 0 in advertisement JSON

- **Root cause**: the JSON encoder emits `report->ts_ms`, but no code ever
  set `ts_ms` — grep for `ts_ms =` returned nothing.
- **Fix**: added `uint32_t ts_ms` to `adv_report_raw_t` (`interfaces/ble_if.h`),
  stamped at reception in the GAP callback (`esp_timer_get_time()/1000`,
  `ble_scan.c`), propagated in `scan_pipeline.c` (`parsed.ts_ms = raw.ts_ms`).
- **Verification**: real monotonic timestamps in captured output
  (608, 612, 622 … 25887 ms).

## N2 — scanning silently died after ~10.24 s

- **Root cause**: NimBLE maps discovery duration `0` to
  `BLE_GAP_DISC_DUR_DFLT` = 10240 ms (`ble_gap.c:4855-4856`). The old
  `DISC_COMPLETE` handler just cleared `s_scanning`, so every scan ended
  after one 10.24 s window.
- **Fix v1**: restart discovery on `DISC_COMPLETE` via a shared
  `s_start_discovery()` helper; `ble_scan_stop` clears `s_scanning`
  *before* `ble_gap_disc_cancel()` so a requested stop never restarts.
- **Fix v1 weakness found by testing**: on scans started immediately after
  boot, the first window could end with *no* DISC_COMPLETE delivered
  (flag stayed true, radio silent, no error). Reproduced twice
  (`capture_25s.py`, uptime 618 ms / 787 ms boots).
- **Fix final**: pass `BLE_HS_FOREVER` to `ble_gap_disc` — NimBLE then runs
  one continuous discovery with no duration timer (`ble_gap.c:4853-4854`),
  eliminating the window boundary entirely. The DISC_COMPLETE restart stays
  as a harmless fallback. `host/ble_hs.h` included for the constant.
- **Verification**:
  - port-held-open capture: 285 adv lines in 25 s, `max ts = 25887 ms` —
    far past the old 10.24 s window, scan started at uptime 787 ms
  - earlier capture: 95 adv lines, `max ts = 150847 ms`
  - `SCAN STOP` still clean (`-411` when not scanning; stop stays stopped)
  - regression: bridge 32/32, power 14/14 after the final firmware

## N3 — closing the COM port mid-scan resets the chip

- **Symptom**: with the host port closed, scans died and the device showed
  fresh-boot state (`idle`, `received:0`, NimBLE uptime back near 0) with
  no panic output.
- **Investigation**: light sleep ruled out (identical behavior with
  `POWER SLEEP OFF`); port-held-open captures never reset; boot uptime
  markers proved repeated silent resets tied to port-close timing.
- **Proof**: added `esp_reset_reason()` to the boot banner and STATUS JSON;
  post-event STATUS reported `reset_reason: 11` = **`ESP_RST_USB`** — the
  ESP32-S3 USB-Serial/JTAG peripheral resets the chip when the host
  releases the interface. This is chip/host interaction, not a firmware
  defect, and is exactly why F4.3 deferred USB suspend handling to v2.
- **Consequence / workaround**: host tools must keep the COM port open
  (the normal operating mode for a collector, and what all hardware suites
  do for in-scan checks). The `test_ble.py` open-close-per-command pattern
  triggers the reset and is unsuitable for long-running checks — use
  `capture_25s.py` (kept as a permanent tool) instead.
- **Observability kept**: `reset_reason` in boot banner and STATUS
  (`1`=poweron, `3`=sw, `4`=panic, `6`=task-wdt, `9`=brownout, `11`=USB).

## Verification (final firmware, COM12, 2026-08-10)

- ESP32 build: BUILD_OK; flash OK
- `capture_25s.py`: 285 adv / 25 s, max ts 25887 ms, no crash markers
- `test_bridge_hw.py`: 32/32
- `test_power_hw.py`: 14/14
- Host tests unaffected by the device-side changes (67/67 earlier today)

## Lessons learned

- Automated suites that only watch the first seconds after SCAN START miss
  window/timing bugs — a 25 s raw-stream capture is now a standing tool.
- `esp_reset_reason()` in STATUS is cheap, decisive field diagnostics —
  silent resets are otherwise invisible when the console port is closed.
- NimBLE `ble_gap_disc(duration_ms=0)` does NOT mean "forever" — it means
  "default window". `BLE_HS_FOREVER` is the continuous-scan constant.
