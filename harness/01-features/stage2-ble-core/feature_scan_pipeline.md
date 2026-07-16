# Feature: Scan Data Pipeline

| Field | Value |
|-------|-------|
| **Feature ID** | F2.3 |
| **Stage** | 2 — BLE Core |
| **Layer** | BLE Core |
| **Dependencies** | F1.1 (USB CDC), F1.2 (AD data parser), F1.3 (filter engine), F2.2 (BLE passive scan) |
| **Source Files** | `interfaces/pipeline_if.h`, `firmware/components/pipeline/scan_pipeline.c` |
| **Test File** | `tests/harness/test_pipeline_on_target.c` |

---

## 1. Description

Wire the BLE scan output into a multi-stage data pipeline that runs in its own FreeRTOS task. The pipeline processes each raw advertisement report through the following stages:

1. **Read** raw report from BLE scan queue (`ble_scan_get_report`)
2. **Parse** AD structures (`proto_parse_adv_data`)
3. **Filter** through the configured filter engine (`filter_evaluate`)
4. **Encode** as JSON (`json_encode_adv`)
5. **Output** via USB CDC console (`usb_console_send_line`)

The pipeline is the central data-path of the sniffer dongle. It connects BLE input to USB output with optional filtering and transformation in between.

---

## 2. Public API

| Function | Signature | Description |
|----------|-----------|-------------|
| `pipeline_init` | `int pipeline_init(void)` | Initialize the pipeline (create task in suspended state). Returns 0 on success. |
| `pipeline_start` | `int pipeline_start(void)` | Resume the pipeline task; begin processing reports. Returns 0 on success. |
| `pipeline_stop` | `int pipeline_stop(void)` | Suspend the pipeline task; flush the BLE scan queue. Returns 0 on success. |
| `pipeline_set_filter_engine` | `int pipeline_set_filter_engine(filter_engine_t *eng)` | Set the filter engine used by the pipeline. NULL disables filtering (pass-all). |
| `pipeline_get_stats` | `int pipeline_get_stats(pipeline_stats_t *stats)` | Copy current pipeline statistics into the caller-provided struct. |

### Data Structures

```c
typedef struct {
    uint32_t total_received;   /* Reports read from BLE queue */
    uint32_t total_filtered;   /* Reports suppressed by filter */
    uint32_t total_output;     /* Reports successfully written to USB */
    uint32_t parse_errors;     /* AD parse failures */
    uint32_t encode_errors;    /* JSON encode failures */
} pipeline_stats_t;
```

### Return / Error Codes

| Code | Meaning |
|------|---------|
| `0` | Success |
| `-802` | NULL pointer passed to a function that requires non-NULL |
| `-806` | Pipeline not initialized (`pipeline_init` not called) |
| `-811` | Invalid state (e.g., start when already running, stop when not running) |

---

## 3. Task Configuration

| Parameter | Value | Notes |
|-----------|-------|-------|
| Task name | `"pipeline"` | Visible in `vTaskList` |
| Priority | **2** | Below BLE task (priority 5+), above USB task (priority 1) |
| Stack size | **4096 bytes** | Must accommodate JSON encoding buffer |
| Core affinity | Core 0 | BLE runs on core 1; pipeline on core 0 |
| Queue read timeout | **100 ms** | Pipeline blocks on `ble_scan_get_report` with 100 ms timeout |

---

## 4. Internal Behavior

### Pipeline Loop

```
while (running) {
    ret = ble_scan_get_report(&raw, 100);
    if (ret == timeout) continue;
    if (ret != 0) { stats.parse_errors++; continue; }

    stats.total_received++;

    ret = proto_parse_adv_data(raw.adv_data, raw.adv_data_len, &parsed);
    if (ret != 0) { stats.parse_errors++; continue; }

    if (filter_engine && !filter_evaluate(filter_engine, &parsed)) {
        stats.total_filtered++;
        continue;
    }

    ret = json_encode_adv(&parsed, json_buf, sizeof(json_buf));
    if (ret != 0) { stats.encode_errors++; continue; }

    ret = usb_console_send_line(json_buf);
    if (ret == 0) stats.total_output++;
}
```

### Stop Behavior

1. Set `running = false`.
2. Notify the pipeline task (via task notification or event group).
3. Drain remaining items from the BLE scan queue (discard).
4. Wait for the pipeline task to acknowledge suspension.
5. Reset per-report local state.

### Filter Engine

- The filter engine is set externally via `pipeline_set_filter_engine()`.
- When NULL, all reports pass through (no filtering).
- The filter engine is not owned by the pipeline — the pipeline only holds a pointer.

### Memory Model

- No `malloc`/`free`. All buffers are statically allocated.
- JSON encoding buffer: 512 bytes on the pipeline task stack.
- Parsed advertisement struct: ~256 bytes on the pipeline task stack.
- Total stack usage: well within the 4096-byte allocation.

---

## 5. Acceptance Criteria

| # | Criterion |
|---|-----------|
| AC-1 | `pipeline_init()` returns `0` and the pipeline task is created (suspended). |
| AC-2 | `pipeline_start()` begins processing; ads appear on USB output. |
| AC-3 | `pipeline_stop()` halts processing; no further USB output. |
| AC-4 | Filter engine suppresses non-matching devices (they are counted in `total_filtered`). |
| AC-5 | Parse errors are counted in `stats.parse_errors` and do not halt the pipeline. |
| AC-6 | Encode errors are counted in `stats.encode_errors` and do not halt the pipeline. |
| AC-7 | `pipeline_get_stats()` returns accurate counters. |
| AC-8 | Pipeline stop flushes the BLE scan queue (no stale reports on restart). |

---

## 6. Test Cases

| ID | Name | Setup | Action | Expected Result |
|----|------|-------|--------|-----------------|
| TC-1 | Init success | BLE initialized | `pipeline_init()` | Returns `0` |
| TC-2 | Start/stop | Pipeline initialized | `pipeline_start()` then `pipeline_stop()` | Both return `0` |
| TC-3 | Process valid adv end-to-end | Pipeline running, BLE queue has report | Inject a valid raw report into BLE queue | JSON line appears on USB output; `total_received` and `total_output` increment |
| TC-4 | Filter suppresses device | Pipeline running, filter engine set to reject a specific MAC | Inject report from rejected MAC | `total_filtered` increments; no USB output for that report |
| TC-5 | Parse error counted | Pipeline running | Inject report with malformed AD data | `parse_errors` increments; pipeline continues |
| TC-6 | Encode error counted | Pipeline running | Force JSON encode failure (e.g., oversized data) | `encode_errors` increments; pipeline continues |
| TC-7 | NULL params | Any state | `pipeline_get_stats(NULL)` | Returns `-802` |
| TC-8 | Start when already running | Pipeline running | `pipeline_start()` | Returns `-811` |
| TC-9 | Stop when not running | Pipeline initialized but stopped | `pipeline_stop()` | Returns `-811` |

---

## 7. Non-Functional Requirements

| Requirement | Constraint |
|-------------|------------|
| Per-report latency | Pipeline must process a single report within **5 ms** (parse + filter + encode + send) |
| Data integrity | No data corruption between pipeline stages; each stage operates on a local copy |
| RAM usage | Pipeline task stack: **4096 bytes**; no additional heap allocation |
| Fault isolation | A parse or encode error does not halt the pipeline; it is counted and the next report is processed |
| Determinism | All buffers are static; no `malloc`/`free` in the pipeline loop |

---

## 8. Interaction with Other Features

| Feature | Relationship |
|---------|-------------|
| F2.2 (BLE scan) | Pipeline consumes from BLE scan queue |
| F1.1 (USB CDC) | Pipeline outputs JSON lines via `usb_console_send_line` |
| F1.2 (AD parser) | Pipeline calls `proto_parse_adv_data` |
| F1.3 (filter engine) | Pipeline calls `filter_evaluate`; filter engine is set externally |
| F3.1 (Lua port) | Future: Lua script can provide custom filter/transform functions that plug into the pipeline |

---

## 9. Open Questions

- Should the pipeline support a transform stage (Lua callback) in Stage 2, or is that deferred to Stage 3?
- Should `pipeline_stop` block until the task is fully suspended, or return immediately?
- What is the maximum JSON line length? Should oversized lines be truncated or dropped?
