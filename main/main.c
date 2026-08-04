#include <stdio.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "usb_if.h"
#include "storage_if.h"
#include "ble_if.h"
#include "lua_if.h"
#include "cli_if.h"
#include "power_if.h"

void app_main(void)
{
    printf("\n=== BLE Sniffer Dongle v1.0.0 ===\n");
    printf("Commands: STATUS, VERSION, SCAN START/STOP/INTERVAL, "
           "FILTER ADD/CLEAR/LIST, LUA INIT/EXEC/DEINIT, "
           "SCRIPT LOAD/BEGIN/CHUNK/END/RUN/STOP/STATUS, POWER\n\n");

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
             * acknowledged silently — only real responses go out. */
            if (response[0] != '\0') {
                usb_console_send_json(response);
                printf("< %s\n", response);
            }
        }
        else if (line_len == -503) {
            /* Timeout - normal */
        }
        else if (line_len < 0) {
            printf("Read error: %d\n", line_len);
        }
    }
}
