/*
 * Host-test shim for ESP-IDF <esp_system.h>.
 *
 * cli_commands.c reports the reset reason in STATUS; on the host we only
 * need the type and a stub value. Kept in tests/host/ (already on the
 * host include path) so firmware builds never see it.
 */
#pragma once

#include <stdint.h>

typedef enum {
    ESP_RST_UNKNOWN = 0,
    ESP_RST_POWERON = 1,
    ESP_RST_EXT = 2,
    ESP_RST_SW = 3,
    ESP_RST_PANIC = 4,
    ESP_RST_INT_WDT = 5,
    ESP_RST_TASK_WDT = 6,
    ESP_RST_WDT = 7,
    ESP_RST_DEEPSLEEP = 8,
    ESP_RST_BROWNOUT = 9,
    ESP_RST_SDIO = 10,
    ESP_RST_USB = 11,
    ESP_RST_JTAG = 12,
} esp_reset_reason_t;

static inline esp_reset_reason_t esp_reset_reason(void)
{
    return ESP_RST_SW;
}

/* H4: STATUS also reports free heap; a fixed value is fine on host */
static inline uint32_t esp_get_free_heap_size(void)
{
    return 123456;
}
