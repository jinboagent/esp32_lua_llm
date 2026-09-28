# Feature: Lua Script Management

| Field | Value |
|-------|-------|
| **Feature ID** | F3.2 |
| **Stage** | 3 — Lua |
| **Layer** | Lua |
| **Dependencies** | F3.1 (Lua port), F0.1 (USB CDC), F0.2 (LittleFS storage) |
| **Source Files** | `interfaces/script_if.h`, `firmware/components/lua/script_mgmt.c` |
| **Test File** | `tests/host/ (Unity host suite) + root Python HIL suites (see README Test strategy)` |

---

## 1. Description

Manage the full lifecycle of user-provided Lua scripts on the BLE sniffer dongle. Scripts are uploaded via USB CDC in a chunked protocol, persisted to LittleFS, and then loaded into the Lua engine for execution. The running script exposes two hooks into the BLE data pipeline:

- **`on_adv`** — called for each advertisement; returns `true` to pass or `false` to suppress.
- **`transform`** — called after filtering; returns a custom JSON string for the output.

Scripts can be stopped at any time. A script error (compile or runtime) does not crash the firmware or halt BLE scanning.

---

## 2. Public API

| Function | Signature | Description |
|----------|-----------|-------------|
| `script_upload_begin` | `int script_upload_begin(void)` | Begin a new script upload session. Returns 0 on success. |
| `script_upload_chunk` | `int script_upload_chunk(const uint8_t *data, uint16_t len)` | Upload a chunk of script data. Returns 0 on success. |
| `script_upload_end` | `int script_upload_end(void)` | Finalize the upload; validate and save to LittleFS. Returns 0 on success. |
| `script_run` | `int script_run(void)` | Load the saved script into the Lua engine and begin execution. Returns 0 on success. |
| `script_stop` | `int script_stop(void)` | Stop the running script. Returns 0 on success. |
| `script_is_loaded` | `bool script_is_loaded(void)` | Returns `true` if a script is loaded (compiled) in the Lua engine. |
| `script_is_running` | `bool script_is_running(void)` | Returns `true` if a script is currently executing hooks. |

### Return / Error Codes

| Code | Meaning |
|------|---------|
| `0` | Success |
| `-602` | NULL pointer passed to a required parameter |
| `-603` | Buffer too small — script exceeds 8 KB maximum |
| `-605` | Upload timeout — more than 5 seconds between chunks |
| `-606` | Not initialized (script management subsystem not ready) |
| `-611` | Invalid state (e.g., upload chunk without begin, run without uploaded script) |
| `-612` | Script compile error (detected at `script_upload_end` or `script_run`) |
| `-613` | Script runtime error (hook execution failed) |
| `-704` | LittleFS storage write failed |

---

## 3. Upload Protocol

### Sequence

<!-- chart-id: CH-luascript-md-01 rev1 -->
```
Host                          Dongle
  |                              |
  |--- script_upload_begin() -->|  Start upload session
  |<---------- 0 ---------------|
  |                              |
  |--- script_upload_chunk() -->|  Send chunk 1 (up to 512 bytes)
  |<---------- 0 ---------------|
  |                              |
  |--- script_upload_chunk() -->|  Send chunk 2
  |<---------- 0 ---------------|
  |          ...                 |
  |                              |
  |--- script_upload_end() ---->|  Finalize and save
  |<---------- 0 ---------------|
```

### Constraints

| Parameter | Value | Notes |
|-----------|-------|-------|
| Maximum script size | **8 KB** | Total across all chunks; exceeded → `-603` |
| Max chunk size | 512 bytes | Per chunk; larger chunks are accepted but count toward 8 KB |
| Inter-chunk timeout | **5 seconds** | If no chunk arrives within 5 s, the upload session is cancelled → `-605` |
| Concurrent uploads | Not allowed | A second `upload_begin` while one is in progress returns `-611` |

### Internal Upload State

```c
typedef struct {
    bool     active;          /* Upload session in progress */
    uint16_t total_received;  /* Bytes received so far */
    uint32_t last_chunk_tick; /* Tick count of last chunk (for timeout) */
    uint8_t  buffer[8192];    /* Static upload buffer (8 KB) */
} script_upload_ctx_t;
```

- The upload buffer is statically allocated (no `malloc`/`free`).
- On `script_upload_end`, the buffer content is written to LittleFS as `/script.lua`.
- The upload buffer is reused across uploads.

---

## 4. Lua Hooks

Scripts may define the following functions. If a hook is not defined, the pipeline uses default behavior.

### `on_adv(addr, addr_type, rssi, name, uuids, manu_id, manu_data) → bool`

Called by the pipeline for each advertisement that passes AD parsing.

| Parameter | Type | Description |
|-----------|------|-------------|
| `addr` | string | BLE address as `"AA:BB:CC:DD:EE:FF"` |
| `addr_type` | integer | 0 = public, 1 = random |
| `rssi` | integer | RSSI in dBm |
| `name` | string\|nil | Device name from AD (nil if absent) |
| `uuids` | table | Array of UUID strings (16-bit and 128-bit) |
| `manu_id` | integer\|nil | Manufacturer company ID (nil if absent) |
| `manu_data` | string\|nil | Manufacturer-specific data as hex string (nil if absent) |

**Return:** `true` = pass (include in output), `false` = suppress (do not output).

### `transform(addr, parsed_data) → string`

Called after `on_adv` returns `true`. Allows the script to customize the JSON output format.

| Parameter | Type | Description |
|-----------|------|-------------|
| `addr` | string | BLE address |
| `parsed_data` | table | Parsed advertisement data (name, uuids, manu_id, etc.) |

**Return:** A JSON string to be sent over USB CDC. If the hook is not defined, the default JSON encoder is used.

---

## 5. Internal Behavior

### Script Execution Model

- The script is compiled once at `script_run()` via `luaL_loadstring`.
- If compilation fails, return `-612` and do not start execution.
- The script's `on_adv` and `transform` functions are called from the pipeline task context.
- Hook calls go through `lua_pcall` — errors are caught and logged, not propagated.
- If a hook raises an error, the pipeline falls back to default behavior (pass-all / default JSON).

### Script Stop

1. Set a `running` flag to `false`.
2. The pipeline checks this flag before calling hooks.
3. If a hook is mid-execution, the Lua execution timeout (10 ms, from F3.1) will terminate it.
4. The compiled script is released from the Lua engine.

### Error Handling

- **Compile error at upload:** `script_upload_end` attempts a trial compile. If it fails, the script is NOT saved to LittleFS, and `-612` is returned.
- **Compile error at run:** `script_run` returns `-612`.
- **Runtime error in hook:** The error message is logged. The pipeline continues with default behavior. The script remains loaded but the failing hook is disabled.
- **Storage write failure:** `script_upload_end` returns `-704`. The upload buffer is discarded.

### Memory Model

- Upload buffer: **8 KB** static.
- No `malloc`/`free` in script management code.
- Lua VM memory is managed by the Lua port (F3.1) custom allocator.

---

## 6. Acceptance Criteria

| # | Criterion |
|---|-----------|
| AC-1 | A script can be uploaded via begin/chunk/end and saved to LittleFS. |
| AC-2 | An upload exceeding 8 KB is rejected with `-603`. |
| AC-3 | An upload with >5 s gap between chunks times out and is cancelled (`-605`). |
| AC-4 | `script_run()` loads the script and the `on_adv` hook is called for incoming ads. |
| AC-5 | `script_run()` without an uploaded script returns `-611`. |
| AC-6 | `script_stop()` halts hook execution; pipeline reverts to default behavior. |
| AC-7 | A script compile error is detected and reported (`-612`) without crashing. |
| AC-8 | A script runtime error in a hook is caught; the pipeline continues operating. |
| AC-9 | Script execution is isolated — a script crash does not affect BLE scanning. |
| AC-10 | Storage full during upload returns `-704`. |

---

## 7. Test Cases

| ID | Name | Setup | Action | Expected Result |
|----|------|-------|--------|-----------------|
| TC-1 | Upload begin/chunk/end success | No upload in progress | `begin()` → `chunk(data, len)` → `end()` | All return `0`; script saved to LittleFS |
| TC-2 | Upload >8KB rejected | Upload in progress | Send chunks totaling >8192 bytes | `upload_chunk` returns `-603` |
| TC-3 | Upload timeout | Upload in progress, one chunk sent | Wait >5 seconds, send next chunk | Returns `-605` |
| TC-4 | Run loaded script | Script uploaded and saved | `script_run()` | Returns `0`; `script_is_loaded()` and `script_is_running()` return `true` |
| TC-5 | Run without script | No script uploaded | `script_run()` | Returns `-611` |
| TC-6 | Stop running script | Script running | `script_stop()` | Returns `0`; `script_is_running()` returns `false` |
| TC-7 | Script on_adv hook called | Script running with `on_adv` defined | BLE ad received | `on_adv` is called with correct parameters; return value controls output |
| TC-8 | Script compile error handled | Script with syntax error uploaded | `script_upload_end()` or `script_run()` | Returns `-612`; firmware continues |
| TC-9 | Script runtime error handled | Script running, hook contains `error("boom")` | BLE ad received | Error logged; pipeline continues with default behavior |
| TC-10 | Storage full during upload | LittleFS nearly full | `script_upload_end()` | Returns `-704` |

---

## 8. Non-Functional Requirements

| Requirement | Constraint |
|-------------|------------|
| Script isolation | A script crash does not affect BLE scanning or firmware stability |
| Upload buffer | **8 KB** static allocation; no heap usage |
| Upload throughput | Must accept chunks at USB CDC line speed (~1 Mbps) without backpressure |
| Storage | Script stored as `/script.lua` on LittleFS; max 8 KB file size |
| Hook latency | `on_adv` hook call must complete within the pipeline's 5 ms per-report budget |
| Determinism | No `malloc`/`free` in script management code |

---

## 9. Interaction with Other Features

| Feature | Relationship |
|---------|-------------|
| F3.1 (Lua port) | Script management uses `lua_engine_exec` and the Lua VM for script compilation and hook execution |
| F0.1 (USB CDC) | Script data is received from the host via USB CDC |
| F0.2 (LittleFS) | Scripts are persisted to LittleFS as `/script.lua` |
| F2.3 (Scan pipeline) | Pipeline calls `on_adv` and `transform` hooks during report processing |

---

## 10. Open Questions

- Should multiple scripts be supported (script slots), or is a single active script sufficient?
- Should the upload protocol include a checksum (e.g., CRC32) for integrity verification?
- Should `transform` hook errors disable only that hook while keeping `on_adv` active, or disable the entire script?

## Implementation Notes (v1.0.0, 2026-08-16)
- `transform(addr, json_string)` - second param is the JSON string, not the spec's parsed table. Deliberate: friendlier for LLM-generated scripts. Documented divergence (bug_check/README.md).
- `on_adv` matches the spec's 7-arg signature.