/*
 * Power Management (F4.3, reduced scope) — power_mgmt.c
 *
 * Delegates sleep entry/exit to the ESP-IDF power manager: with
 * CONFIG_PM_ENABLE + tickless idle, the SoC light-sleeps whenever all
 * tasks block and wakes on USB console activity. While the device
 * streams scan data the CLI holds the no-light-sleep activity lock
 * (power_hold_activity) — during light sleep the USB-Serial/JTAG
 * console TX FIFO cannot drain and console output is dropped.
 *
 * Zero allocation: no malloc/free in this module.
 */

#include <stdio.h>
#include <string.h>
#include "sdkconfig.h"
#include "power_if.h"
#include "ble_if.h"
#include "script_if.h"

#if CONFIG_PM_ENABLE
#include "esp_pm.h"
#endif

#define POWER_CPU_MAX_MHZ   CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ
#define POWER_CPU_MIN_MHZ   40

/* Calibrated estimates for the ESP32-S3 devkit (no fuel gauge on v1
 * hardware) — documented in power_if.h. */
#define POWER_MA_SCANNING      45
#define POWER_MA_ACTIVE_IDLE   30
#define POWER_MA_LIGHT_SLEEP    8

static power_config_t s_config = {
    .sleep_enabled    = true,
    .idle_timeout_ms  = 1000,
    .usb_suspend_wake = false,   /* v2 placeholder */
};

static bool s_initialized = false;

#if CONFIG_PM_ENABLE
/* No-light-sleep lock held while the device streams data (scanning). */
static esp_pm_lock_handle_t s_activity_lock = NULL;
static bool s_activity_held = false;
#endif

static void s_apply_policy(void)
{
#if CONFIG_PM_ENABLE
    esp_pm_config_t pm = {
        .max_freq_mhz       = POWER_CPU_MAX_MHZ,
        .min_freq_mhz       = POWER_CPU_MIN_MHZ,
        .light_sleep_enable = s_config.sleep_enabled,
    };
    esp_err_t err = esp_pm_configure(&pm);
    if (err != ESP_OK) {
        printf("Power: esp_pm_configure failed (%d)\n", (int)err);
    } else {
        printf("Power: light sleep %s\n",
               s_config.sleep_enabled ? "enabled" : "disabled");
    }
#else
    printf("Power: CONFIG_PM_ENABLE not set — system stays active\n");
#endif
}

int power_init(void)
{
    s_initialized = true;
#if CONFIG_PM_ENABLE
    if (s_activity_lock == NULL) {
        esp_err_t err = esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0,
                                           "ble_activity", &s_activity_lock);
        if (err != ESP_OK) {
            s_activity_lock = NULL;
            /* B2 fix: without the activity lock the SoC could light-sleep
             * mid-scan and the console TX FIFO would drop output. Fail
             * safe: keep the device awake and return an error so the boot
             * log surfaces the problem instead of silently degrading. */
            s_config.sleep_enabled = false;
            printf("Power: activity lock create failed (%d) — "
                   "light sleep disabled\n", (int)err);
            s_apply_policy();
            return -1;
        }
    }
#endif
    s_apply_policy();
    return 0;
}

void power_hold_activity(bool hold)
{
#if CONFIG_PM_ENABLE
    if (s_activity_lock == NULL) {
        return;
    }
    if (hold && !s_activity_held) {
        esp_pm_lock_acquire(s_activity_lock);
        s_activity_held = true;
    } else if (!hold && s_activity_held) {
        esp_pm_lock_release(s_activity_lock);
        s_activity_held = false;
    }
#else
    (void)hold;
#endif
}

int power_enable_sleep(bool enable)
{
    s_config.sleep_enabled = enable;
    if (s_initialized) {
        s_apply_policy();
    }
    return 0;
}

power_state_t power_get_state(void)
{
    if (ble_scan_is_active() || script_is_running()) {
        return POWER_STATE_ACTIVE;
    }
    return s_config.sleep_enabled ? POWER_STATE_LIGHT_SLEEP
                                  : POWER_STATE_ACTIVE;
}

int power_get_current_ma(uint32_t *current_ma)
{
    if (current_ma == NULL) {
        return POWER_ERR_NULL;
    }

    if (ble_scan_is_active()) {
        *current_ma = POWER_MA_SCANNING;
    } else if (s_config.sleep_enabled) {
        *current_ma = POWER_MA_LIGHT_SLEEP;
    } else {
        *current_ma = POWER_MA_ACTIVE_IDLE;
    }
    return 0;
}

int power_get_config(power_config_t *config)
{
    if (config == NULL) {
        return POWER_ERR_NULL;
    }
    memcpy(config, &s_config, sizeof(*config));
    return 0;
}
