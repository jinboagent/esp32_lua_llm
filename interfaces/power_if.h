#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Power Management Interface (F4.3, reduced scope)
 *
 * Power-saving via ESP-IDF automatic light sleep: when every task is
 * blocked (no scan running, no script running, CLI waiting for input)
 * the SoC enters light sleep automatically and wakes on USB console
 * activity or BLE/RToS events. No manual sleep calls, no wake-source
 * bookkeeping — and therefore no sleeping mid-scan.
 *
 * Scope reductions vs. the original F4.3 spec (documented in the
 * feature doc):
 *  - USB bus-suspend detection is deferred to v2: the USB-Serial/JTAG
 *    console peripheral exposes no suspend signal on ESP32-S3.
 *  - power_get_current_ma() returns calibrated estimates, not PMIC
 *    readings (the devkit has no fuel gauge).
 *
 * Error codes:
 *   0      Success
 *   -502   NULL pointer
 */

#define POWER_ERR_NULL  (-502)

typedef enum {
    POWER_STATE_ACTIVE = 0,
    POWER_STATE_LIGHT_SLEEP,
    POWER_STATE_USB_SUSPEND   /* reserved — not detectable on v1 hardware */
} power_state_t;

typedef struct {
    bool     sleep_enabled;       /* Automatic light sleep when idle      */
    uint32_t idle_timeout_ms;     /* Advisory; tickless idle is governed
                                     by FreeRTOS, not polled             */
    bool     usb_suspend_wake;    /* v2 placeholder — always false       */
} power_config_t;

/*
 * Initialize power management and apply the default policy
 * (sleep enabled, idle timeout 1000 ms). Requires CONFIG_PM_ENABLE
 * for actual sleep entry; without it the system stays active and a
 * notice is logged.
 */
int power_init(void);

/*
 * Enable or disable automatic light sleep. Takes effect immediately
 * (reconfigures the ESP-IDF power manager).
 */
int power_enable_sleep(bool enable);

/*
 * Current policy/state snapshot.
 * state: ACTIVE while scanning or a script is running; LIGHT_SLEEP
 * when idle with sleep enabled (the SoC enters/exits light sleep
 * automatically between tasks).
 */
power_state_t power_get_state(void);

/*
 * Estimated current draw in mA (calibrated lookup, not measured):
 *   ~45 mA scanning (radio RX), ~30 mA active idle, ~8 mA light-sleep
 * idle. -502 if current_ma is NULL.
 */
int power_get_current_ma(uint32_t *current_ma);

/*
 * Copy the active configuration. -502 if config is NULL.
 */
int power_get_config(power_config_t *config);

/*
 * Hold or release the no-light-sleep activity lock.
 *
 * Held while the device streams data (BLE scanning) so console output
 * is never delayed or dropped by sleep entry — the USB-Serial/JTAG
 * console TX drops bytes once its FIFO cannot drain, which happens
 * while the SoC light-sleeps. Call symmetrically: hold on SCAN START
 * success, release on SCAN STOP. Safe to call when PM is compiled out.
 */
void power_hold_activity(bool hold);

#ifdef __cplusplus
}
#endif
