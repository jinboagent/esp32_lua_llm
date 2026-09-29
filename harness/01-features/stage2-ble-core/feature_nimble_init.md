# Feature: NimBLE Stack Initialization

| Field | Value |
|-------|-------|
| **Feature ID** | F2.1 |
| **Stage** | 2 — BLE Core |
| **Layer** | BLE Core |
| **Dependencies** | None |
| **Source Files** | `interfaces/ble_if.h`, `firmware/components/ble/ble_nimble_init.c` |
| **Test File** | `tests/host/ (Unity host suite) + root Python HIL suites (see README Test strategy)` |

---

## 1. Description

Initialize the NimBLE BLE stack on the ESP32-S3 in host-only mode (no GATT server). Configure the GAP layer with a public device address, set the device name to `"BLE-Bridge"`, and set the GAP appearance to the generic sensor icon. After initialization the stack must be ready to begin passive scanning.

This feature owns the full lifecycle of the NimBLE host: start, health check, and shutdown.

---

## 2. Public API

| Function | Signature | Description |
|----------|-----------|-------------|
| `ble_init` | `int ble_init(void)` | Initialize NimBLE host stack, configure GAP. Returns 0 on success. |
| `ble_deinit` | `int ble_deinit(void)` | Shut down NimBLE host, release resources. Returns 0 on success. |
| `ble_is_ready` | `bool ble_is_ready(void)` | Returns `true` if the BLE stack is initialized and ready for scanning. |

### Return / Error Codes

| Code | Meaning |
|------|---------|
| `0` | Success |
| `-7` | Already initialized (returned by `ble_init` when called a second time) |
| `-6` | Not initialized (returned by `ble_deinit` or `ble_is_ready` when stack was never started) |

---

## 3. Configuration Defaults

| Parameter | Default Value | Notes |
|-----------|---------------|-------|
| Mode | Host-only | No GATT server registered; no services, no characteristics |
| Address type | Public | Use the factory MAC address |
| Device name | `"BLE-Bridge"` | Set via `ble_gap_set_device_name()` |
| Appearance | Generic Sensor (0x0300) | GAP appearance characteristic value |
| Bonding | Disabled | No pairing or bonding |
| MTU | Default (256) | No custom MTU negotiation needed |

---

## 4. Internal Behavior

1. `ble_init()` calls `nimble_port_init()` and `esp_nimble_hci_init()`.
2. Configures the GAP event callback (only for sync/unsync events; no connection events).
3. Sets device name and appearance.
4. Sets an internal `ready` flag to `true` once the host syncs.
5. `ble_deinit()` calls `nimble_port_stop()` and `nimble_port_deinit()`, clears the `ready` flag.
6. All functions are idempotent with respect to their error codes — calling init twice returns `-7`, not a crash.

### Memory Model

- No `malloc`/`free` in application code. NimBLE internally manages its own mbuf pool.
- The NimBLE stack consumes approximately **60 KB RAM** (configurable via `sdkconfig` mbuf pool size).
- All application-side state is stored in a single static struct (`ble_ctx_t`) allocated at file scope.

---

## 5. Acceptance Criteria

| # | Criterion |
|---|-----------|
| AC-1 | `ble_init()` returns `0` on first call and the stack is ready for scanning. |
| AC-2 | `ble_is_ready()` returns `true` after successful init. |
| AC-3 | `ble_deinit()` returns `0` and `ble_is_ready()` returns `false` afterward. |
| AC-4 | Calling `ble_init()` a second time returns `-7` without side effects. |
| AC-5 | Calling `ble_deinit()` without prior init returns `-6`. |

---

## 6. Test Cases

| ID | Name | Setup | Action | Expected Result |
|----|------|-------|--------|-----------------|
| TC-1 | Init success | Stack not initialized | Call `ble_init()` | Returns `0` |
| TC-2 | Deinit success | Stack initialized | Call `ble_deinit()` | Returns `0` |
| TC-3 | is_ready after init | Stack initialized | Call `ble_is_ready()` | Returns `true` |
| TC-4 | Double init returns -7 | Stack initialized | Call `ble_init()` again | Returns `-7`, stack still functional |
| TC-5 | Deinit without init | Stack not initialized | Call `ble_deinit()` | Returns `-6` |

---

## 7. Non-Functional Requirements

| Requirement | Constraint |
|-------------|------------|
| Init latency | `ble_init()` must complete within **500 ms** |
| RAM usage | NimBLE stack uses approximately **60 KB** (mbuf pool + host structures) |
| Thread safety | `ble_init` / `ble_deinit` are NOT thread-safe — caller must serialize |
| Determinism | No heap allocation in application code; all state is static |

---

## 8. sdkconfig Knobs

```
CONFIG_BT_ENABLED=y
CONFIG_BT_NIMBLE_ENABLED=y
CONFIG_BT_NIMBLE_ROLE_PERIPHERAL=n
CONFIG_BT_NIMBLE_ROLE_CENTRAL=n
CONFIG_BT_NIMBLE_ROLE_OBSERVER=y
CONFIG_BT_NIMBLE_ROLE_BROADCASTER=y
CONFIG_BT_NIMBLE_MEM_POOL_SIZE=70
```

---

## 9. Open Questions

- None at this time.

## Implementation Notes (v1.0.0, 2026-08-16)
- `ROLE_BROADCASTER` not enabled - observer-only sdkconfig is leaner than the spec's role list (deviation now documented).
- NimBLE host task is NOT pinned to core 1 - the spec's core-separation claim is not enforced; task priorities decouple the work. See docs/evaluation-response-2026-08-16.md.