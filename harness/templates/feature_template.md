# Feature: <Feature Name>

## Basic Info

| Field        | Value                        |
|--------------|------------------------------|
| Feature ID   | F<stage>.<seq>               |
| Stage        | <0-4> — <stage name>         |
| Layer        | <HAL / Core / Integration>   |
| Dependencies | <list of feature IDs this depends on> |
| Source Files | `interfaces/<name>_if.h`, `firmware/components/<name>/<file>.c` |
| Test Files   | `tests/harness/test_<name>_on_target.c` |

## Functional Description

<Describe what this feature does, its role in the system, and key design
decisions. Include state machines, protocol flows, or data flow diagrams
as ASCII art where applicable.>

### State Machine (if applicable)

```
 STATE_A ──event──▶ STATE_B ──event──▶ STATE_C
   ▲                                      │
   └──────────── event ──────────────────┘
```

## I/O Definitions

### Error Codes

| Code  | Meaning        |
|-------|----------------|
| 0     | Success        |
| -NNN  | <description>  |

### Structs / Enums / Macros

```c
/* Enum: <name>_state_t — <purpose> */
typedef enum {
    <NAME>_STATE_A = 0,
    <NAME>_STATE_B
} <name>_state_t;

/* Struct: <name>_config_t — <purpose> */
typedef struct {
    <type> <field>;   /* <description> */
} <name>_config_t;
```

### Public Functions

#### `<module>_init`

```c
int <module>_init(void);
```

| Param | Direction | Description |
|-------|-----------|-------------|
| —     | —         | —           |

**Returns**: `0` on success, negative error code on failure.

<Description of what this function does.>

#### `<module>_<action>`

```c
int <module>_<action>(<type> <param>, <type> *<out>);
```

| Param   | Direction | Description            |
|---------|-----------|------------------------|
| `param` | in        | <description>          |
| `out`   | out       | <description>          |

**Returns**: `0` on success, negative error code on failure.

<Description of what this function does.>

## Acceptance Criteria

1. <Quantifiable, testable criterion.>
2. <Each criterion should be verifiable by a test case.>
3. <Avoid vague language; use numbers, ranges, exact behaviours.>

## Exception Scenarios

<Must cover at minimum:>
- NULL pointer parameters.
- Buffer overflow / undersized buffer.
- Invalid state transitions.
- Resource exhaustion (storage, buffer full).
- Timeout conditions.
- Interruption of multi-step operations.

## Non-Functional Constraints

| Constraint     | Requirement                                                    |
|----------------|----------------------------------------------------------------|
| Memory         | Zero heap allocation; stack usage < N bytes per call           |
| Reentrant      | <Yes/No — specify which functions if partial>                  |
| Performance    | <latency / throughput requirement>                             |
| Dependencies   | <list of modules / drivers this depends on>                    |
| Stack          | C11, ESP-IDF v5.x, NimBLE stack                                |

## Test Cases

| ID   | Scenario              | Input / Condition          | Expected Output / Behaviour    |
|------|-----------------------|----------------------------|--------------------------------|
| TC-1 | <scenario>            | <input>                    | <expected>                     |
| TC-2 | <scenario>            | <input>                    | <expected>                     |
| TC-N | <scenario>            | <input>                    | <expected>                     |

## Build & Test Commands

```bash
# Host-side unit test (mocked dependencies)
cd tests/harness
cmake -B build -S . && cmake --build build
./build/test_<name>_on_target

# On-target test via ESP-IDF
idf.py -C tests/harness build flash monitor
```
