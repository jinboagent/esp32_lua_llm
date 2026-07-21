#include "ble_if.h"
#include <stdio.h>
#include "esp_err.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "services/gap/ble_svc_gap.h"

/* Required by NimBLE for bonding/storage */
void ble_store_config_init(void);

#define BLE_SYNC_BIT  BIT0

static bool s_initialized = false;
static EventGroupHandle_t s_sync_event_group = NULL;

static void s_on_sync(void)
{
    if (s_sync_event_group) {
        xEventGroupSetBits(s_sync_event_group, BLE_SYNC_BIT);
    }
}

static void s_on_reset(int reason)
{
    printf("BLE: host reset, reason=%d\n", reason);
}

static void s_host_task(void *param)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

int ble_init(void)
{
    if (s_initialized) {
        return -7;
    }

    /* Initialize NVS — required for PHY calibration data */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        printf("BLE: NVS init failed: %s\n", esp_err_to_name(ret));
        return -1;
    }

    /* Initialize NimBLE (includes HCI + controller init internally) */
    ret = nimble_port_init();
    if (ret != ESP_OK) {
        printf("BLE: nimble_port_init failed: %d\n", (int)ret);
        return -1;
    }

    /* Create sync event group */
    s_sync_event_group = xEventGroupCreate();
    if (s_sync_event_group == NULL) {
        nimble_port_deinit();
        return -1;
    }

    /* Configure host callbacks */
    ble_hs_cfg.sync_cb = s_on_sync;
    ble_hs_cfg.reset_cb = s_on_reset;

    /* Set device name */
    ble_svc_gap_device_name_set("BLE-Sniffer");

    /* Initialize storage config (required by NimBLE host) */
    ble_store_config_init();

    /* Start NimBLE host task */
    nimble_port_freertos_init(s_host_task);

    /* Wait for host-controller sync (up to 2 seconds) */
    EventBits_t bits = xEventGroupWaitBits(
        s_sync_event_group, BLE_SYNC_BIT,
        pdTRUE, pdFALSE, pdMS_TO_TICKS(2000));

    if (!(bits & BLE_SYNC_BIT)) {
        printf("BLE: host sync timeout\n");
        nimble_port_stop();
        nimble_port_deinit();
        vEventGroupDelete(s_sync_event_group);
        s_sync_event_group = NULL;
        return -5;
    }

    s_initialized = true;
    printf("BLE: NimBLE initialized, device name: BLE-Sniffer\n");
    return 0;
}

int ble_deinit(void)
{
    if (!s_initialized) {
        return -6;
    }

    nimble_port_stop();
    nimble_port_deinit();

    if (s_sync_event_group) {
        vEventGroupDelete(s_sync_event_group);
        s_sync_event_group = NULL;
    }

    s_initialized = false;
    return 0;
}

bool ble_is_ready(void)
{
    return s_initialized;
}
