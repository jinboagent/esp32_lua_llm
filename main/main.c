#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "proto_if.h"
#include "json_if.h"
#include "filter_if.h"
#include "usb_if.h"
#include "storage_if.h"
#include "ble_if.h"

/* Global filter engine (shared between CLI and pipeline) */
static filter_engine_t s_filter_engine;

/* Command parser */
static void process_command(const char *cmd, char *response, uint16_t response_len)
{
    if (strcmp(cmd, "STATUS") == 0) {
        pipeline_stats_t stats;
        pipeline_get_stats(&stats);
        snprintf(response, response_len,
            "{\"status\":\"ok\",\"cmd\":\"status\","
            "\"scanning\":%s,"
            "\"filter_count\":%d,"
            "\"script_loaded\":false,"
            "\"script_running\":false,"
            "\"pipeline\":{\"received\":%lu,\"filtered\":%lu,"
            "\"output\":%lu,\"parse_err\":%lu,\"encode_err\":%lu}}",
            ble_scan_is_active() ? "true" : "false",
            filter_get_count(&s_filter_engine),
            (unsigned long)stats.total_received,
            (unsigned long)stats.total_filtered,
            (unsigned long)stats.total_output,
            (unsigned long)stats.parse_errors,
            (unsigned long)stats.encode_errors);
    }
    else if (strcmp(cmd, "VERSION") == 0) {
        snprintf(response, response_len,
            "{\"status\":\"ok\",\"cmd\":\"version\",\"firmware\":\"0.2.0\","
            "\"build_date\":\"%s\",\"chip\":\"esp32s3\"}", __DATE__);
    }
    else if (strncmp(cmd, "SCAN ", 5) == 0) {
        const char *action = cmd + 5;
        if (strcmp(action, "START") == 0) {
            if (!ble_is_ready()) {
                snprintf(response, response_len,
                    "{\"status\":\"error\",\"cmd\":\"scan_start\","
                    "\"msg\":\"BLE not initialized\"}");
            } else if (ble_scan_is_active()) {
                snprintf(response, response_len,
                    "{\"status\":\"error\",\"cmd\":\"scan_start\","
                    "\"msg\":\"already scanning\"}");
            } else {
                int ret1 = ble_scan_start();
                int ret2 = pipeline_start();
                if (ret1 == 0 && ret2 == 0) {
                    snprintf(response, response_len,
                        "{\"status\":\"ok\",\"cmd\":\"scan_start\"}");
                } else {
                    snprintf(response, response_len,
                        "{\"status\":\"error\",\"cmd\":\"scan_start\","
                        "\"msg\":\"start failed: scan=%d pipeline=%d\"}",
                        ret1, ret2);
                }
            }
        }
        else if (strcmp(action, "STOP") == 0) {
            if (!ble_scan_is_active()) {
                snprintf(response, response_len,
                    "{\"status\":\"error\",\"cmd\":\"scan_stop\","
                    "\"msg\":\"not scanning\"}");
            } else {
                pipeline_stop();
                ble_scan_stop();
                snprintf(response, response_len,
                    "{\"status\":\"ok\",\"cmd\":\"scan_stop\"}");
            }
        }
        else {
            snprintf(response, response_len,
                "{\"status\":\"error\",\"cmd\":\"scan\",\"msg\":\"unknown action\"}");
        }
    }
    else if (strncmp(cmd, "FILTER ", 7) == 0) {
        const char *action = cmd + 7;
        if (strcmp(action, "CLEAR") == 0) {
            filter_clear(&s_filter_engine);
            snprintf(response, response_len,
                "{\"status\":\"ok\",\"cmd\":\"filter_clear\"}");
        }
        else if (strcmp(action, "LIST") == 0) {
            snprintf(response, response_len,
                "{\"status\":\"ok\",\"cmd\":\"filter_list\","
                "\"count\":%d}",
                filter_get_count(&s_filter_engine));
        }
        else {
            snprintf(response, response_len,
                "{\"status\":\"error\",\"cmd\":\"filter\",\"msg\":\"unknown action\"}");
        }
    }
    else {
        snprintf(response, response_len,
            "{\"status\":\"error\",\"msg\":\"unknown command\"}");
    }
}

void app_main(void)
{
    printf("\n=== BLE Sniffer Dongle v0.2.0 ===\n");
    printf("Commands: STATUS, VERSION, SCAN START/STOP, FILTER CLEAR/LIST\n\n");

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

    /* Initialize filter engine */
    filter_init(&s_filter_engine);

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
            process_command(cmd_buf, response, sizeof(response));
            usb_console_send_json(response);
            printf("< %s\n", response);
        }
        else if (line_len == -503) {
            /* Timeout - normal */
        }
        else if (line_len < 0) {
            printf("Read error: %d\n", line_len);
        }
    }
}
