#include <stdio.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_system.h"

#include "usb_if.h"
#include "storage_if.h"
#include "ble_if.h"
#include "lua_if.h"
#include "cli_if.h"
#include "bridge_if.h"
#include "pack_if.h"
#include "power_if.h"

#if CONFIG_BLE_CONN_ENABLED
/* F2.4 lifecycle hook: hold the no-light-sleep lock while a connection
 * streams, release it when the link drops. Runs in the ble host task. */
static void s_conn_event(bool connected, const uint8_t *addr)
{
    (void)addr;
    power_hold_conn(connected);
}
#endif

void app_main(void)
{
    printf("\n=== BLE Bridge Dongle v1.0.0 ===\n");
    /* Boot observability: esp_reset_reason() — 1=poweron, 3=sw, 4=panic,
     * 6=task-wdt, 9=brownout, 11=USB. USB resets occur when the host
     * closes the COM port mid-scan (USB-Serial/JTAG chip behavior). */
    printf("Reset reason: %d\n", (int)esp_reset_reason());
    printf("Commands: STATUS, VERSION, SCAN START/STOP/INTERVAL, "
           "FILTER ADD/CLEAR/LIST, LUA INIT/EXEC/BEGIN/END/DEINIT, "
           "PACK LIST/BEGIN/END/RUN/DEL/AUTORUN, "
           "SCRIPT LOAD/BEGIN/CHUNK/END/RUN/STOP/STATUS, POWER"
#ifdef CONFIG_BLE_CONN_ENABLED
           ", CONN TARGET/START/STOP/STATUS/INTERVAL"
#endif
           "\n");
    printf("Ctrl+C: stop scan/script/upload/connection immediately\n\n");

    /* Initialize USB console */
    int ret = usb_console_init();
    bool usb_ok = (ret == 0);
    if (!usb_ok) {
        printf("WARNING: USB console init failed (%d)\n", ret);
    }

    /* Initialize storage */
    ret = storage_init();
    if (ret != 0) {
        printf("WARNING: Storage init failed (%d)\n", ret);
    } else {
        uint32_t free_space = 0;
        storage_get_free_space(&free_space);
        printf("Storage: %lu bytes free\n", (unsigned long)free_space);
    }

    /* Initialize CLI (owns the filter engine shared with the pipeline) */
    ret = cli_init();
    if (ret != 0) {
        printf("WARNING: CLI init failed (%d)\n", ret);
    }

    /* Initialize BLE stack */
    ret = ble_init();
    if (ret != 0) {
        printf("WARNING: BLE init failed (%d)\n", ret);
    }

#if CONFIG_BLE_CONN_ENABLED
    /* Initialize the optional connection feature (F2.4). The event
     * callback wires the power hold here in main — the ble component
     * must not depend on power (power already depends on ble). */
    ret = ble_conn_init();
    if (ret != 0) {
        printf("WARNING: BLE conn init failed (%d)\n", ret);
    } else {
        ble_conn_set_event_cb(s_conn_event);
    }
#endif

    /* Initialize pipeline (suspended, waiting for SCAN START) */
    ret = pipeline_init();
    if (ret != 0) {
        printf("WARNING: Pipeline init failed (%d)\n", ret);
    }
    pipeline_set_filter(cli_get_filter_engine());

    /* Initialize Lua engine */
    ret = lua_engine_init();
    if (ret != 0) {
        printf("WARNING: Lua engine init failed (%d)\n", ret);
    }

    /* Initialize LLM bridge (text-line script upload state machine).
     * B3 fix: was never called — it only worked by static zero-init. */
    ret = bridge_init();
    if (ret != 0) {
        printf("WARNING: Bridge init failed (%d)\n", ret);
    }

    /* H6.1 M2: tool packs — create /littlefs/packs and execute every
     * pack carrying an autorun marker, so registered tools are alive
     * before any host connects (decision 13's reserved seam, honored). */
    ret = pack_store_init();
    if (ret != 0) {
        printf("WARNING: Pack store init failed (%d)\n", ret);
    }
    ret = pack_store_boot_autorun();
    if (ret < 0) {
        printf("WARNING: Pack autorun failed (%d)\n", ret);
    } else if (ret > 0) {
        printf("Pack autorun: %d pack(s) active\n", ret);
    }

    /* Initialize power management (automatic light sleep when idle) */
    ret = power_init();
    if (ret != 0) {
        printf("WARNING: Power init failed (%d)\n", ret);
    }

    printf("Ready. Type SCAN START to begin.\n\n");

    char cmd_buf[USB_RX_BUFFER_SIZE];
    char response[USB_TX_BUFFER_SIZE];

    while (1) {
        if (!usb_ok) {
            vTaskDelay(pdMS_TO_TICKS(5000));
            printf("Heartbeat (USB disabled)\n");
            continue;
        }

        int line_len = usb_console_read_line(cmd_buf, sizeof(cmd_buf), 1000);

        if (line_len > 0) {
            printf("> %s\n", cmd_buf);
            cli_process_command(cmd_buf, response, sizeof(response));
            /* Script data lines during a F4.2 text-line upload are
             * acknowledged silently — only real responses go out.
             * L-S4-3 fix: send_json already emits the response; the old
             * extra printf("< ...") duplicated every response on the
             * console stream. */
            if (response[0] != '\0') {
                usb_console_send_json(response);
            }
        }
        else if (line_len == -503) {
            /* Timeout - normal */
        }
        else if (line_len == -504) {
            /* P2 (audit B6 device side): the USB layer dropped an
             * overlong line. Mid-upload that means silently-missing
             * text — abort the session and tell the host in strict
             * JSON instead of a plain-text line no tool parses. */
            if (cli_abort_uploads()) {
                snprintf(response, sizeof(response),
                    "{\"status\":\"error\",\"cmd\":\"read\","
                    "\"code\":-504,\"msg\":\"line too long "
                    "(max %d bytes); upload aborted\"}",
                    USB_RX_BUFFER_SIZE - 1);
                usb_console_send_json(response);
            } else {
                printf("Read error: -504\n");
            }
        }
        else if (line_len < 0) {
            printf("Read error: %d\n", line_len);
        }
    }
}
