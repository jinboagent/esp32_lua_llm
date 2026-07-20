#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "proto_if.h"
#include "json_if.h"
#include "filter_if.h"
#include "usb_if.h"
#include "storage_if.h"

/* Command parser */
static void process_command(const char *cmd, char *response, uint16_t response_len)
{
    if (strcmp(cmd, "STATUS") == 0) {
        snprintf(response, response_len,
            "{\"status\":\"ok\",\"cmd\":\"status\",\"scanning\":false,"
            "\"filter_count\":0,\"script_loaded\":false,\"script_running\":false}");
    }
    else if (strcmp(cmd, "VERSION") == 0) {
        snprintf(response, response_len,
            "{\"status\":\"ok\",\"cmd\":\"version\",\"firmware\":\"0.1.0\","
            "\"build_date\":\"%s\",\"chip\":\"esp32s3\"}", __DATE__);
    }
    else if (strncmp(cmd, "SCAN ", 5) == 0) {
        const char *action = cmd + 5;
        if (strcmp(action, "START") == 0) {
            snprintf(response, response_len,
                "{\"status\":\"ok\",\"cmd\":\"scan_start\"}");
        }
        else if (strcmp(action, "STOP") == 0) {
            snprintf(response, response_len,
                "{\"status\":\"ok\",\"cmd\":\"scan_stop\"}");
        }
        else {
            snprintf(response, response_len,
                "{\"status\":\"error\",\"cmd\":\"scan\",\"msg\":\"unknown action\"}");
        }
    }
    else if (strncmp(cmd, "FILTER ", 7) == 0) {
        const char *action = cmd + 7;
        if (strcmp(action, "CLEAR") == 0) {
            snprintf(response, response_len,
                "{\"status\":\"ok\",\"cmd\":\"filter_clear\"}");
        }
        else if (strcmp(action, "LIST") == 0) {
            snprintf(response, response_len,
                "{\"status\":\"ok\",\"cmd\":\"filter_list\",\"filters\":[]}");
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
    printf("\n=== BLE Sniffer Dongle v0.1.0 ===\n");
    printf("Type commands (STATUS, VERSION, SCAN START/STOP, FILTER CLEAR/LIST)\n\n");

    /* Initialize USB console */
    int ret = usb_console_init();
    bool usb_ok = (ret == 0);
    if (!usb_ok) {
        printf("WARNING: USB console init failed (%d), commands disabled\n", ret);
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

    char cmd_buf[USB_RX_BUFFER_SIZE];
    char response[USB_TX_BUFFER_SIZE];

    while (1) {
        if (!usb_ok) {
            /* USB not available, just heartbeat */
            vTaskDelay(pdMS_TO_TICKS(5000));
            printf("Heartbeat (USB commands disabled)\n");
            continue;
        }

        /* Try to read a command from USB */
        int line_len = usb_console_read_line(cmd_buf, sizeof(cmd_buf), 1000);

        if (line_len > 0) {
            /* Got a command */
            printf("> %s\n", cmd_buf);

            /* Process and respond */
            process_command(cmd_buf, response, sizeof(response));
            usb_console_send_json(response);
            printf("< %s\n", response);
        }
        else if (line_len == -503) {
            /* Timeout - normal, just heartbeat */
        }
        else if (line_len < 0) {
            printf("Read error: %d\n", line_len);
        }
    }
}
