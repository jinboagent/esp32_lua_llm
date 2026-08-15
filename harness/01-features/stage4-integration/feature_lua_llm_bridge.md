# Feature: LLM Script Bridge

## Basic Info

| Field        | Value                                        |
|--------------|----------------------------------------------|
| Feature ID   | F4.2                                         |
| Stage        | 4 — Integration                              |
| Layer        | Integration                                  |
| Dependencies | F3.2 (lua_script_mgmt), F0.1 (usb_cdc)      |
| Source Files | `interfaces/bridge_if.h`, `firmware/components/bridge/lua_llm_bridge.c` |
| Test Files   | `tests/host/ (Unity host suite) + root Python HIL suites (see README Test strategy)`      |

## Functional Description

Handles the protocol for receiving LLM-generated Lua scripts from the host PC
over USB CDC, validating syntax, and deploying to the Lua engine.

### Upload Protocol Flow

```
  PC                          Device
  │                              │
  │── SCRIPT LOAD ──────────────▶│  (CLI command)
  │◀── {"status":"ok",...} ─────│  respond "ready"
  │                              │
  │── script line 1 ────────────▶│  buffered internally
  │── script line 2 ────────────▶│
  │── ... ──────────────────────▶│
  │                              │
  │── SCRIPT END ───────────────▶│  (CLI command)
  │                              │  validate via luaL_loadstring()
  │◀── {"status":"ok",...} ─────│  respond "saved" or "compile error"
  │                              │
```

The bridge module:
1. Enters upload mode on `SCRIPT LOAD`.
2. Accumulates script text in a static 8 KB buffer.
3. On `SCRIPT END`, validates Lua syntax via `luaL_loadstring()`.
4. On success, hands the compiled chunk to `lua_script_mgmt` for deployment.
5. On failure, returns the compile error message and discards the buffer.

No `malloc`/`free` is used; the script buffer is a file-scope `static char[8192]`.

## I/O Definitions

### Error Codes

| Code  | Meaning                |
|-------|------------------------|
| 0     | Success                |
| -612  | Script compile error   |
| -704  | Storage write failed   |
| -802  | NULL pointer           |
| -803  | Buffer too small       |

### Enum: bridge_state_t

```c
typedef enum {
    BRIDGE_STATE_IDLE = 0,
    BRIDGE_STATE_UPLOADING,
    BRIDGE_STATE_VALIDATED,
    BRIDGE_STATE_RUNNING
} bridge_state_t;
```

### Struct: bridge_upload_result_t

```c
typedef struct {
    int         error_code;     /* 0 = success, negative = error           */
    const char *error_msg;      /* Human-readable error (NULL on success)  */
    uint32_t    script_size;    /* Bytes accumulated                       */
} bridge_upload_result_t;
```

### Public Functions

#### `bridge_init`

```c
int bridge_init(void);
```

| Param | Direction | Description |
|-------|-----------|-------------|
| —     | —         | —           |

**Returns**: `0` on success, negative error code on failure.

Initialize the bridge subsystem. Reset upload buffer and state to IDLE.

#### `bridge_handle_script_upload`

```c
int bridge_handle_script_upload(
    const char *script_text,
    uint32_t    len,
    char       *response,
    uint16_t    response_len
);
```

| Param          | Direction | Description                                        |
|----------------|-----------|----------------------------------------------------|
| `script_text`  | in        | Script content (may be partial during upload)      |
| `len`          | in        | Length of `script_text` in bytes                   |
| `response`     | out       | Caller-allocated buffer for JSON response          |
| `response_len` | in        | Size of `response` buffer in bytes                 |

**Returns**: `0` on success, negative error code on failure.

Called by the CLI layer for each chunk of script data. On the final chunk
(SCRIPT END), validates and deploys.

## Acceptance Criteria

1. A valid Lua script (<= 8 KB) is accepted, compiled, and deployable —
   subsequent `SCRIPT RUN` executes it.
2. A script with a syntax error is rejected; the response includes the
   `luaL_loadstring` error message.
3. A script exceeding 8192 bytes is rejected with error code `-803`.
4. An upload in progress is aborted if a non-script CLI command (e.g.,
   `SCAN START`) is received; buffer is reset.
5. Storage-full condition during deployment returns `-704`.
6. An empty script (0 bytes at SCRIPT END) is rejected with `-612`.
7. Scripts containing sandbox-violating calls (`os.execute`, `io.popen`,
   `dofile`) are rejected with `-612`.
8. NULL `script_text` or `response` returns `-802`.
9. No `malloc`/`free` calls in the bridge module.

## Exception Scenarios

- NULL parameters passed to `bridge_handle_script_upload`.
- Script exceeds 8 KB static buffer.
- Lua compile error (syntax error, unknown global reference).
- Storage write failure during deployment.
- Upload interrupted by CLI command (SCAN START, FILTER CLEAR, etc.).
- Empty script submitted (SCRIPT END with 0 bytes accumulated).
- Script contains sandbox-violating API calls.
- Response buffer too small for error JSON.

## Non-Functional Constraints

| Constraint     | Requirement                                                    |
|----------------|----------------------------------------------------------------|
| Memory         | Static 8 KB script buffer; zero heap allocation                |
| Reentrant      | Not required (single upload at a time, sequential protocol)    |
| Performance    | Syntax validation < 50 ms for 8 KB script                      |
| Dependencies   | lua_script_mgmt (deploy), USB CDC driver (transport)           |
| Stack          | C11, ESP-IDF v5.x, NimBLE stack                                |

## Test Cases

| ID   | Scenario                          | Input                                              | Expected Output / Behaviour                             |
|------|-----------------------------------|----------------------------------------------------|---------------------------------------------------------|
| TC-1 | Valid script upload               | `SCRIPT LOAD` → valid Lua lines → `SCRIPT END`     | Compile success; `{"status":"ok","cmd":"script_end","size":N}` |
| TC-2 | Script with syntax error          | `SCRIPT LOAD` → `function foo(` → `SCRIPT END`     | Compile error; response includes `luaL_loadstring` message; error code -612 |
| TC-3 | Script > 8 KB                     | Accumulate 8193 bytes                              | Rejected with error code `-803`                         |
| TC-4 | Upload interruption               | `SCRIPT LOAD` → partial script → `SCAN START`      | Upload aborted; buffer reset; SCAN START proceeds       |
| TC-5 | Storage full                      | Valid script, storage write fails                  | Error code `-704`; response includes storage error msg  |
| TC-6 | Empty script                      | `SCRIPT LOAD` → `SCRIPT END` (0 bytes)             | Error code `-612`; "empty script" message               |
| TC-7 | Sandbox violation                 | Script containing `os.execute("ls")`               | Error code `-612`; "sandbox violation" message          |
| TC-8 | NULL params                       | `bridge_handle_script_upload(NULL, ...)`           | Error code `-802`                                       |

## Build & Test Commands

```bash
# Host-side unit test (mocked dependencies)
cd <project root>
cmake -B build -S . && cmake --build build
./build/test_bridge_on_target

# On-target test via ESP-IDF
python test_bridge_hw.py / test_power_hw.py / test_ble_lua_hw.py (HIL)
```

## Implementation Notes (v1.0.0, 2026-08-16)
- AC-4 as shipped: any recognized CLI command aborts the upload and then dispatches; unrecognized lines are script text; Ctrl+C always aborts. (The 2026-08-15 evaluation misread this branch.)