# Feature: Power Management

## Basic Info

| Field        | Value                                  |
|--------------|----------------------------------------|
| Feature ID   | F4.3                                   |
| Stage        | 4 — Integration                        |
| Layer        | Integration                            |
| Dependencies | F2.3 (scan_pipeline)                   |
| Source Files | `interfaces/power_if.h`, `firmware/components/power/power_mgmt.c` |
| Test Files   | `tests/host/ (Unity host suite) + root Python HIL suites (see README Test strategy)` |

## Functional Description

Implements power-saving strategies for the BLE sniffer dongle:

1. **Light sleep between scan intervals** — When the scan pipeline is idle
   (waiting for the next scan window), the system enters light sleep to
   reduce current draw. Wake source: timer (next scan interval) or USB
   activity.

2. **USB suspend detection** — When the USB host suspends the bus (no SOF
   packets for 3 ms), the device reduces power consumption to < 2.5 mA
   (USB suspend spec). Wake on USB resume or configurable GPIO.

3. **Wake-on-command** — Any CLI command received over USB CDC immediately
   wakes the system from light sleep.

### Power States

```
  ACTIVE ──(idle timeout)──▶ LIGHT_SLEEP ──(timer/USB)──▶ ACTIVE
     │                                                       │
     │  (USB suspend)                                        │
     ▼                                                       │
  USB_SUSPEND ──(USB resume)─────────────────────────────────┘
```

No `malloc`/`free` is used in this module.

## I/O Definitions

### Error Codes

| Code  | Meaning        |
|-------|----------------|
| 0     | Success        |
| -502  | NULL pointer   |

### Enum: power_state_t

```c
typedef enum {
    POWER_STATE_ACTIVE = 0,
    POWER_STATE_LIGHT_SLEEP,
    POWER_STATE_USB_SUSPEND
} power_state_t;
```

### Struct: power_config_t

```c
typedef struct {
    bool     sleep_enabled;       /* Light sleep between scan intervals  */
    uint32_t idle_timeout_ms;     /* ms of inactivity before sleep       */
    bool     usb_suspend_wake;    /* Wake on USB resume                  */
} power_config_t;
```

### Public Functions

#### `power_init`

```c
int power_init(void);
```

| Param | Direction | Description |
|-------|-----------|-------------|
| —     | —         | —           |

**Returns**: `0` on success, negative error code on failure.

Initialize power management. Configure wake sources (timer, USB, GPIO).
Set default config: sleep enabled, idle timeout 1000 ms, USB suspend wake on.

#### `power_enable_sleep`

```c
int power_enable_sleep(bool enable);
```

| Param   | Direction | Description                       |
|---------|-----------|-----------------------------------|
| `enable`| in        | `true` to enable light sleep      |

**Returns**: `0` on success, negative error code on failure.

Enable or disable light sleep between scan intervals. When disabled, the
system stays in ACTIVE state continuously.

#### `power_get_current_ma`

```c
int power_get_current_ma(uint32_t *current_ma);
```

| Param        | Direction | Description                            |
|--------------|-----------|----------------------------------------|
| `current_ma` | out       | Estimated current draw in milliamps    |

**Returns**: `0` on success, `-502` if `current_ma` is NULL.

Read the estimated current consumption. On ESP32-S3 this is derived from
the PMIC sensor or a calibrated lookup table based on active peripherals.

## Acceptance Criteria

1. Light sleep reduces current draw by at least 40% compared to active idle.
2. Wake from light sleep completes within 5 ms (timer wake) or 10 ms
   (USB wake).
3. Full scan pipeline operation resumes within 50 ms of wake.
4. USB suspend is detected within 100 ms of bus suspend.
5. USB suspend current is < 2.5 mA.
6. USB resume restores full operation within 100 ms.
7. `power_enable_sleep(false)` keeps the system in ACTIVE state indefinitely.
8. NULL `current_ma` returns `-502`.
9. No `malloc`/`free` calls in the power module.

## Exception Scenarios

- NULL pointer passed to `power_get_current_ma`.
- Sleep enabled but scan pipeline not yet initialized (graceful: stay active).
- USB suspend occurs during active script execution (defer sleep until script
   yields or completes).
- Wake source configuration fails (log error, remain in active mode).
- Repeated sleep/wake cycles cause no state corruption over 1000+ cycles.

## Non-Functional Constraints

| Constraint     | Requirement                                                    |
|----------------|----------------------------------------------------------------|
| Memory         | Zero heap allocation; stack usage < 256 bytes per call         |
| Reentrant      | `power_get_current_ma` must be safe to call from any task      |
| Performance    | Sleep entry/exit < 5 ms (timer wake)                           |
| Dependencies   | scan_pipeline (idle signal), USB CDC driver (suspend/resume)   |
| Stack          | C11, ESP-IDF v5.x, NimBLE stack                                |

## Test Cases

| ID   | Scenario                  | Input / Condition                         | Expected Output / Behaviour                          |
|------|---------------------------|-------------------------------------------|------------------------------------------------------|
| TC-1 | Init                      | `power_init()`                            | Returns 0; state = ACTIVE; default config applied    |
| TC-2 | Enable/disable sleep      | `power_enable_sleep(true)` then `(false)` | Returns 0; state transitions work correctly          |
| TC-3 | Wake from sleep           | Enter light sleep, fire timer wake        | State → ACTIVE within 5 ms; scan pipeline resumes    |
| TC-4 | USB suspend detection     | Suspend USB bus                           | State → USB_SUSPEND within 100 ms; current < 2.5 mA |
| TC-5 | NULL params               | `power_get_current_ma(NULL)`              | Returns `-502`                                       |

## Build & Test Commands

```bash
# Host-side unit test (mocked dependencies)
cd <project root>
cmake -B build -S . && cmake --build build
./build/test_power_on_target

# On-target test via ESP-IDF
python test_bridge_hw.py / test_power_hw.py / test_ble_lua_hw.py (HIL)
```

## Implementation Notes (v1.0.0, 2026-08-04)

Shipped scope differs from the original spec in three documented ways:

1. **Automatic light sleep instead of manual sleep entry.** With
   `CONFIG_PM_ENABLE=y` + `CONFIG_FREERTOS_USE_TICKLESS_IDLE=y`, the SoC
   light-sleeps whenever all tasks block and wakes on USB console
   activity. No sleep-entry code, no timer wake bookkeeping.
2. **No-light-sleep activity lock while streaming.** During light sleep
   the USB-Serial/JTAG console TX FIFO cannot drain and the console
   drops output, so the CLI holds an `ESP_PM_NO_LIGHT_SLEEP` lock
   (`power_hold_activity`) between SCAN START and SCAN STOP. Light
   sleep therefore applies only when truly idle — which is the state
   this feature targets ("between scan intervals" collapses to "idle"
   under continuous scanning).
3. **Deferred to v2:** USB bus-suspend detection (USB-Serial/JTAG
   exposes no suspend signal on this hardware) and PMIC-based current
   measurement (`power_get_current_ma` returns calibrated estimates:
   45 mA scanning / 30 mA active idle / 8 mA light-sleep idle).

Hardware-verified: wake-on-command after idle, advertisement streaming
intact with PM enabled, `POWER SLEEP ON/OFF` + `POWER STATUS` CLI
(extension commands, see F4.1 notes).

