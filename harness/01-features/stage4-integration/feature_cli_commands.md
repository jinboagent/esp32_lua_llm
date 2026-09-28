# Feature: CLI Command Interface

## Basic Info

| Field        | Value                                      |
|--------------|--------------------------------------------|
| Feature ID   | F4.1                                       |
| Stage        | 4 — Integration                            |
| Layer        | Integration                                |
| Dependencies | F2.3 (scan_pipeline), F3.2 (lua_script_mgmt) |
| Source Files | `interfaces/cli_if.h`, `firmware/components/cli/cli_commands.c` |
| Test Files   | `tests/host/ (Unity host suite) + root Python HIL suites (see README Test strategy)`       |

## Functional Description

Full CLI command interface over USB CDC. Parses text commands from the host,
dispatches to the appropriate subsystem (scan pipeline, filter, Lua script
manager), and returns structured JSON responses.

All responses are single-line JSON terminated by `\n`. The parser is
zero-allocation: it operates on a caller-supplied response buffer and never
calls `malloc`/`free`.

### State Machine

<!-- chart-id: CH-cli-md-01 rev1 -->
```
 IDLE ──SCAN START──▶ SCANNING ──SCRIPT RUN──▶ SCRIPT_RUNNING
  ▲                     │                            │
  │   SCAN STOP         │   SCAN STOP               │   SCRIPT STOP
  └─────────────────────┴────────────────────────────┘

 outside this machine (no IDLE/SCANNING/SCRIPT_RUNNING guard):
   PACK BEGIN/LIST/RUN/DEL/AUTORUN · LUA INIT/EXEC/BEGIN/END · POWER *
   CONN TARGET/STATUS/STOP/INTERVAL — and CONN START, which is refused
   while SCRIPT_RUNNING; the conn plane has its own state machine
   (see firmware/components/ble/README.md)
 Ctrl+C (0x03) interrupts scan/script/upload/conn from any state.
```

Commands restricted per state:
- **IDLE**: all commands available except SCAN STOP, SCRIPT RUN, SCRIPT STOP.
- **SCANNING**: SCAN START rejected; filter modification rejected; SCRIPT RUN allowed.
- **SCRIPT_RUNNING**: SCAN START/STOP, FILTER *, SCRIPT RUN rejected; SCRIPT STOP allowed.
- **CONN START** is also rejected while SCRIPT_RUNNING (conn plane is state-guarded separately).

## I/O Definitions

### Error Codes

| Code  | Meaning              |
|-------|----------------------|
| 0     | Success              |
| -901  | Invalid command      |
| -902  | NULL pointer         |
| -911  | Invalid state        |

### Enum: cli_state_t

```c
typedef enum {
    CLI_STATE_IDLE = 0,
    CLI_STATE_SCANNING,
    CLI_STATE_SCRIPT_RUNNING
} cli_state_t;
```

### Public Functions

#### `cli_init`

```c
int cli_init(void);
```

| Param | Direction | Description                  |
|-------|-----------|------------------------------|
| —     | —         | —                          |

**Returns**: `0` on success, negative error code on failure.

Initialize the CLI subsystem. Register USB CDC receive callback. Set initial
state to `CLI_STATE_IDLE`.

#### `cli_process_command`

```c
int cli_process_command(const char *cmd, char *response, uint16_t response_len);
```

| Param         | Direction | Description                                      |
|---------------|-----------|--------------------------------------------------|
| `cmd`         | in        | Null-terminated command string from host         |
| `response`    | out       | Caller-allocated buffer for JSON response        |
| `response_len`| in        | Size of `response` buffer in bytes               |

**Returns**: `0` on success, negative error code on failure.

Parse `cmd`, validate against current state, dispatch, and write JSON into
`response`.

### Command Reference

| Command                  | Success Response                                                  | Error Response                                                        |
|--------------------------|-------------------------------------------------------------------|-----------------------------------------------------------------------|
| `SCAN START`             | `{"status":"ok","cmd":"scan_start"}`                              | `{"status":"error","cmd":"scan_start","msg":"already scanning"}`      |
| `SCAN STOP`              | `{"status":"ok","cmd":"scan_stop"}`                               | `{"status":"error","cmd":"scan_stop","msg":"not scanning"}`           |
| `SCAN INTERVAL <ms>`     | `{"status":"ok","cmd":"scan_interval","value":N}`                 | `{"status":"error","cmd":"scan_interval","msg":"invalid value"}`      |
| `FILTER ADD name <pat>`  | `{"status":"ok","cmd":"filter_add","index":N}`                    | `{"status":"error","cmd":"filter_add","msg":"..."}`                   |
| `FILTER ADD uuid <uuid>` | `{"status":"ok","cmd":"filter_add","index":N}`                    | `{"status":"error","cmd":"filter_add","msg":"..."}`                   |
| `FILTER ADD rssi <thr>`  | `{"status":"ok","cmd":"filter_add","index":N}`                    | `{"status":"error","cmd":"filter_add","msg":"..."}`                   |
| `FILTER ADD mac <addr>`  | `{"status":"ok","cmd":"filter_add","index":N}`                    | `{"status":"error","cmd":"filter_add","msg":"..."}`                   |
| `FILTER CLEAR`           | `{"status":"ok","cmd":"filter_clear"}`                            | `{"status":"error","cmd":"filter_clear","msg":"..."}`                 |
| `FILTER LIST`            | `{"status":"ok","cmd":"filter_list","filters":[...]}`             | `{"status":"error","cmd":"filter_list","msg":"..."}`                  |
| `SCRIPT LOAD`            | `{"status":"ok","cmd":"script_load","msg":"ready"}`               | `{"status":"error","cmd":"script_load","msg":"..."}`                  |
| `SCRIPT END`             | `{"status":"ok","cmd":"script_end","size":N}`                     | `{"status":"error","cmd":"script_end","msg":"..."}`                   |
| `SCRIPT RUN`             | `{"status":"ok","cmd":"script_run"}`                              | `{"status":"error","cmd":"script_run","msg":"..."}`                   |
| `SCRIPT STOP`            | `{"status":"ok","cmd":"script_stop"}`                             | `{"status":"error","cmd":"script_stop","msg":"..."}`                  |
| `STATUS`                 | `{"status":"ok","cmd":"status","scanning":bool,"filter_count":N,"script_loaded":bool,"script_running":bool,"free_storage":N}` | — |
| `VERSION`                | `{"status":"ok","cmd":"version","firmware":"1.0.0","build_date":"...","chip":"esp32s3"}` | — |
| Ctrl+C (0x03, no Enter)  | `{"status":"ok","cmd":"interrupt"}` — aborts upload, stops script and scan | `{"status":"error","cmd":"interrupt","msg":"stop failed: r1/r2"}` |
| `<invalid>`              | —                                                                 | `{"status":"error","msg":"unknown command"}`                          |
| `<syntax error>`         | —                                                                 | `{"status":"error","msg":"invalid syntax: expected ..."}`             |

## Acceptance Criteria

1. Every command in the Command Reference table produces the exact JSON
   structure shown, terminated by `\n`.
2. State restrictions are enforced: SCAN START in SCANNING returns error,
   FILTER ADD in SCANNING returns error, SCRIPT RUN in IDLE returns error.
3. Invalid/unrecognised commands return `{"status":"error","msg":"unknown command"}`.
4. Malformed syntax returns `{"status":"error","msg":"invalid syntax: expected ..."}`
   with a hint about the expected format.
5. No `malloc`/`free` calls in the CLI module (verified by static analysis).
6. Response buffer overflow is prevented: if the JSON would exceed
   `response_len`, return `-903` and write a truncated error JSON.
7. NULL `cmd` or `response` returns `-902`.

## Exception Scenarios

- NULL pointer passed to `cli_process_command`.
- Response buffer too small for the JSON output.
- Command issued in wrong state (e.g., SCAN START while already scanning).
- Filter modification attempted while scanning is active.
- Script commands attempted while no script is loaded.
- Malformed command with partial tokens (e.g., `FILTER ADD` with no type).
- Numeric parameter out of range (e.g., `SCAN INTERVAL 0`).

## Non-Functional Constraints

| Constraint     | Requirement                                                    |
|----------------|----------------------------------------------------------------|
| Memory         | Zero heap allocation; stack usage < 512 bytes per call         |
| Reentrant      | Not required (single USB CDC consumer task)                    |
| Performance    | Command parse + dispatch < 1 ms                                |
| Dependencies   | scan_pipeline, lua_script_mgmt, USB CDC driver                 |
| Stack          | C11, ESP-IDF v5.x, NimBLE stack                                |

## Test Cases

| ID   | Scenario                              | Input                                | Expected Output / Behaviour                            |
|------|---------------------------------------|--------------------------------------|--------------------------------------------------------|
| TC-1 | SCAN START in IDLE                    | `"SCAN START"` in IDLE state         | `{"status":"ok","cmd":"scan_start"}`, state → SCANNING  |
| TC-2 | SCAN START when already scanning      | `"SCAN START"` in SCANNING state     | `{"status":"error","cmd":"scan_start","msg":"already scanning"}` |
| TC-3 | SCAN STOP                             | `"SCAN STOP"` in SCANNING state      | `{"status":"ok","cmd":"scan_stop"}`, state → IDLE       |
| TC-4 | FILTER ADD name                       | `"FILTER ADD name test*"`            | `{"status":"ok","cmd":"filter_add","index":0}`          |
| TC-5 | FILTER ADD uuid                       | `"FILTER ADD uuid 0x180F"`           | `{"status":"ok","cmd":"filter_add","index":1}`          |
| TC-6 | FILTER CLEAR                          | `"FILTER CLEAR"`                     | `{"status":"ok","cmd":"filter_clear"}`, filter count → 0 |
| TC-7 | FILTER LIST                           | `"FILTER LIST"` with 2 filters       | `{"status":"ok","cmd":"filter_list","filters":[...]}` with 2 entries |
| TC-8 | STATUS in various states              | `"STATUS"` in IDLE, SCANNING, SCRIPT_RUNNING | Correct boolean flags for each state          |
| TC-9 | VERSION                               | `"VERSION"`                          | `{"status":"ok","cmd":"version","firmware":"1.0.0","build_date":"...","chip":"esp32s3"}` |
| TC-10| Unknown command                       | `"BLAH"`                             | `{"status":"error","msg":"unknown command"}`            |
| TC-11| Syntax error                          | `"FILTER ADD"` (missing type + arg)  | `{"status":"error","msg":"invalid syntax: expected ..."}` |
| TC-12| Modify filter while scanning rejected | `"FILTER ADD name x"` in SCANNING    | `{"status":"error","cmd":"filter_add","msg":"..."}` + error code -911 |
| TC-13| SCRIPT LOAD/RUN/STOP sequence         | `"SCRIPT LOAD"` → `"SCRIPT END"` → `"SCRIPT RUN"` → `"SCRIPT STOP"` | Each returns ok; state transitions SCANNING → SCRIPT_RUNNING → SCANNING |

## Build & Test Commands

```bash
# Host-side unit test (mocked dependencies)
cd <project root>
cmake -B build -S . && cmake --build build
./build/test_cli_on_target

# On-target test via ESP-IDF
python test_bridge_hw.py / test_power_hw.py / test_ble_lua_hw.py (HIL)
```
