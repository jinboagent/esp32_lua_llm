# Feature: BLE Passive Scan

| Field | Value |
|-------|-------|
| **Feature ID** | F2.2 |
| **Stage** | 2 — BLE Core |
| **Layer** | BLE Core |
| **Dependencies** | F2.1 (NimBLE Stack Initialization) |
| **Source Files** | `interfaces/ble_if.h`, `firmware/components/ble/ble_scan.c` |
| **Test File** | `tests/host/ (Unity host suite) + root Python HIL suites (see README Test strategy)` |

---

## 1. Description

Start and stop passive BLE scanning on the ESP32-S3. The scan operates in the BLE callback context: raw advertisement reports are copied into a FreeRTOS queue (depth 32) for downstream consumption. The BLE callback performs **no** data processing — it only enqueues.

Scan parameters (interval and window) are configurable within defined bounds. A deduplication mechanism suppresses duplicate reports from the same MAC address within a configurable time window (default 1 second).

---

## 2. Public API

| Function | Signature | Description |
|----------|-----------|-------------|
| `ble_scan_start` | `int ble_scan_start(void)` | Begin passive scanning with current parameters. Returns 0 on success. |
| `ble_scan_stop` | `int ble_scan_stop(void)` | Stop scanning. Returns 0 on success. |
| `ble_scan_set_params` | `int ble_scan_set_params(uint32_t interval_ms, uint32_t window_ms)` | Configure scan interval and window in milliseconds. |
| `ble_scan_is_active` | `bool ble_scan_is_active(void)` | Returns `true` if scanning is currently running. |
| `ble_scan_get_report` | `int ble_scan_get_report(adv_report_raw_t *out, uint32_t timeout_ms)` | Dequeue the next raw advertisement report. Blocks up to `timeout_ms`. |

### Data Structures

```c
typedef struct {
    uint8_t  addr[6];       /* BLE device address (public or random) */
    uint8_t  addr_type;     /* 0=public, 1=random */
    int8_t   rssi;          /* RSSI in dBm */
    uint8_t  adv_type;      /* Advertisement type */
    uint8_t  adv_data_len;  /* Length of adv_data (0..31) */
    uint8_t  adv_data[31];  /* Raw advertisement data */
} adv_report_raw_t;
```

### Return / Error Codes

| Code | Meaning |
|------|---------|
| `0` | Success |
| `-401` | Invalid parameter (e.g., interval out of range, window > interval) |
| `-402` | NULL pointer passed to `ble_scan_get_report` |
| `-406` | BLE stack not initialized (`ble_init` not called) |
| `-411` | Invalid state (e.g., start when already scanning, stop when not scanning) |

---

## 3. Scan Parameters

| Parameter | Default | Min | Max | Unit |
|-----------|---------|-----|-----|------|
| Interval | 100 | 10 | 10240 | ms |
| Window | 50 | 10 | 10240 | ms |

**Constraint:** `window_ms` must be less than or equal to `interval_ms`. Violation returns `-401`.

---

## 4. Internal Behavior

### BLE Callback (NimBLE GAP event handler)

1. Receives `BLE_GAP_EVENT_DISC` from NimBLE.
2. Checks dedup table: if the same MAC address was reported within the last N seconds (default 1), drop the report silently.
3. Copies the raw advertisement data into an `adv_report_raw_t`.
4. Sends to the FreeRTOS queue via `xQueueSend()` with `ticks_to_wait = 0` (non-blocking).
5. If the queue is full, the **oldest entry is dropped** (overwrite mode: `xQueueOverwrite` semantics via a custom ring buffer, or `xQueueSend` with timeout 0 and accept loss).
6. The callback **must return within 1 ms** — no parsing, no filtering, no allocation.

### Deduplication

- A simple hash table indexed by MAC address stores the last-seen timestamp.
- Default dedup window: **1 second** (compile-time configurable via `CONFIG_BLE_SCAN_DEDUP_WINDOW_MS`).
- Entries expire automatically after the window elapses.

### Queue

- FreeRTOS queue, depth **32**, item size = `sizeof(adv_report_raw_t)`.
- Created during `ble_scan_start`, deleted during `ble_scan_stop`.
- On queue overflow: oldest report is discarded, newest is enqueued.

### Memory Model

- No `malloc`/`free` in application code. Queue and dedup table are statically allocated.
- Queue buffer: `32 * sizeof(adv_report_raw_t)` bytes (~1.5 KB).
- Dedup table: fixed-size array of 64 entries (hash bucket), each holding MAC + timestamp.

---

## 5. Acceptance Criteria

| # | Criterion |
|---|-----------|
| AC-1 | `ble_scan_start()` begins passive scanning and advertisement reports are enqueued. |
| AC-2 | `ble_scan_stop()` halts scanning; no further reports are enqueued. |
| AC-3 | `ble_scan_set_params(0, 50)` returns `-401` (interval below minimum). |
| AC-4 | `ble_scan_set_params(50, 100)` returns `-401` (window > interval). |
| AC-5 | `ble_scan_get_report()` delivers raw reports with correct MAC, RSSI, and adv data. |
| AC-6 | Deduplication suppresses duplicate MAC reports within the configured window. |
| AC-7 | Queue overflow drops the oldest report without crashing. |

---

## 6. Test Cases

| ID | Name | Setup | Action | Expected Result |
|----|------|-------|--------|-----------------|
| TC-1 | Start success | BLE initialized, scan not running | `ble_scan_start()` | Returns `0`, `ble_scan_is_active()` returns `true` |
| TC-2 | Stop success | Scan running | `ble_scan_stop()` | Returns `0`, `ble_scan_is_active()` returns `false` |
| TC-3 | Set valid params | BLE initialized | `ble_scan_set_params(200, 100)` | Returns `0` |
| TC-4 | Set invalid interval (0) | BLE initialized | `ble_scan_set_params(0, 50)` | Returns `-401` |
| TC-5 | Set window > interval | BLE initialized | `ble_scan_set_params(50, 100)` | Returns `-401` |
| TC-6 | Start when already scanning | Scan running | `ble_scan_start()` | Returns `-411` |
| TC-7 | Stop when not scanning | Scan not running | `ble_scan_stop()` | Returns `-411` |
| TC-8 | Get report with timeout | Scan running, ads present | `ble_scan_get_report(&report, 1000)` | Returns `0`, report populated |
| TC-9 | Get report queue empty | Scan running, no ads | `ble_scan_get_report(&report, 100)` | Returns timeout error (non-zero, non-negative-specific) |
| TC-10 | NULL params | Any state | `ble_scan_get_report(NULL, 1000)` | Returns `-402` |

---

## 7. Non-Functional Requirements

| Requirement | Constraint |
|-------------|------------|
| Callback latency | BLE GAP callback must return within **1 ms** |
| Queue overflow | Oldest report is dropped; no crash, no corruption |
| Dedup accuracy | Same MAC must not produce more than 1 report per dedup window |
| RAM usage | Queue + dedup table: approximately **2 KB** static |
| Determinism | No heap allocation during scan operation |

---

## 8. Interaction with Downstream Features

- The scan queue is the **input** to the Scan Data Pipeline (F2.3 / `scan_pipeline`).
- The pipeline task calls `ble_scan_get_report()` to consume reports.
- If no consumer is reading, the queue fills and oldest reports are silently dropped.

---

## 9. Open Questions

- Should the dedup window be runtime-configurable (via Lua script) or compile-time only?
- Should scan filter policies (e.g., only accept connectable) be exposed in the API?

## Implementation Notes (v1.0.0, 2026-08-16)
- Dedup table holds 128 entries (spec: 64) - superset.
- Queue overflow drops and counts the NEWEST report (spec said drop-oldest) - never blocks the radio; loss observable via `queue_drops`.