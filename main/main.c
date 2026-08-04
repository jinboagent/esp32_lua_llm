#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "proto_if.h"
#include "json_if.h"
#include "filter_if.h"
#include "usb_if.h"
#include "storage_if.h"
#include "ble_if.h"
#include "lua_if.h"
#include "script_if.h"

/* Global filter engine (shared between CLI and pipeline) */
static filter_engine_t s_filter_engine;

/* Command parser */
static int s_hex_nibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void process_command(const char *cmd, char *response, uint16_t response_len)
{
    if (strcmp(cmd, "STATUS") == 0) {
        pipeline_stats_t stats;
        pipeline_get_stats(&stats);
        snprintf(response, response_len,
            "{\"status\":\"ok\",\"cmd\":\"status\","
            "\"scanning\":%s,"
            "\"queue_drops\":%lu,"
            "\"filter_count\":%d,"
            "\"lua_ready\":%s,"
            "\"script_loaded\":%s,"
            "\"script_running\":%s,"
            "\"pipeline\":{\"received\":%lu,\"filtered\":%lu,"
            "\"output\":%lu,\"parse_err\":%lu,\"encode_err\":%lu}}",
            ble_scan_is_active() ? "true" : "false",
            (unsigned long)ble_scan_get_drop_count(),
            filter_get_count(&s_filter_engine),
            lua_engine_is_ready() ? "true" : "false",
            script_is_loaded() ? "true" : "false",
            script_is_running() ? "true" : "false",
            (unsigned long)stats.total_received,
            (unsigned long)stats.total_filtered,
            (unsigned long)stats.total_output,
            (unsigned long)stats.parse_errors,
            (unsigned long)stats.encode_errors);
    }
    else if (strcmp(cmd, "VERSION") == 0) {
        snprintf(response, response_len,
            "{\"status\":\"ok\",\"cmd\":\"version\",\"firmware\":\"0.3.0\","
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
                if (ret1 != 0) {
                    snprintf(response, response_len,
                        "{\"status\":\"error\",\"cmd\":\"scan_start\","
                        "\"msg\":\"scan failed: %d\"}", ret1);
                } else {
                    int ret2 = pipeline_start();
                    if (ret2 != 0) {
                        /* Rollback: scan started but pipeline failed (B-S3-7 fix) */
                        ble_scan_stop();
                        snprintf(response, response_len,
                            "{\"status\":\"error\",\"cmd\":\"scan_start\","
                            "\"msg\":\"pipeline failed: %d\"}", ret2);
                    } else {
                        snprintf(response, response_len,
                            "{\"status\":\"ok\",\"cmd\":\"scan_start\"}");
                    }
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
            lua_engine_lock();  /* B-S3-6 fix: protect filter from concurrent pipeline access */
            filter_clear(&s_filter_engine);
            lua_engine_unlock();
            snprintf(response, response_len,
                "{\"status\":\"ok\",\"cmd\":\"filter_clear\"}");
        }
        else if (strcmp(action, "LIST") == 0) {
            snprintf(response, response_len,
                "{\"status\":\"ok\",\"cmd\":\"filter_list\","
                "\"count\":%d}",
                filter_get_count(&s_filter_engine));
        }
        else if (strncmp(action, "ADD ", 4) == 0) {
            /* FILTER ADD <type> <value> */
            const char *args = action + 4;
            char type_str[16] = {0};
            char value[FILTER_PATTERN_MAX_LEN] = {0};
            int parsed = sscanf(args, "%15s %31s", type_str, value);

            if (parsed < 2) {
                snprintf(response, response_len,
                    "{\"status\":\"error\",\"cmd\":\"filter_add\","
                    "\"msg\":\"usage: FILTER ADD <NAME|UUID|MAC|RSSI> <value>\"}");
            } else {
                filter_type_t ftype;
                int8_t rssi_val = 0;
                if (strcmp(type_str, "NAME") == 0) {
                    ftype = FILTER_TYPE_NAME;
                } else if (strcmp(type_str, "UUID") == 0) {
                    ftype = FILTER_TYPE_UUID;
                } else if (strcmp(type_str, "MAC") == 0) {
                    ftype = FILTER_TYPE_MAC;
                } else if (strcmp(type_str, "RSSI") == 0) {
                    ftype = FILTER_TYPE_RSSI;
                    /* Validate range before narrowing to int8_t (M-S3-7 fix —
                     * atoi("999") wrapped silently) */
                    char *end = NULL;
                    long v = strtol(value, &end, 10);
                    if (end == value || *end != '\0' || v < -128 || v > 127) {
                        snprintf(response, response_len,
                            "{\"status\":\"error\",\"cmd\":\"filter_add\","
                            "\"msg\":\"RSSI must be a number in -128..127\"}");
                        goto filter_done;
                    }
                    rssi_val = (int8_t)v;
                } else {
                    snprintf(response, response_len,
                        "{\"status\":\"error\",\"cmd\":\"filter_add\","
                        "\"msg\":\"unknown type: %s\"}", type_str);
                    goto filter_done;
                }

                lua_engine_lock();  /* B-S3-6 fix */
                int ret = filter_add_rule(&s_filter_engine, ftype,
                    ftype == FILTER_TYPE_RSSI ? NULL : value, rssi_val);
                lua_engine_unlock();
                if (ret == 0) {
                    snprintf(response, response_len,
                        "{\"status\":\"ok\",\"cmd\":\"filter_add\","
                        "\"type\":\"%s\",\"value\":\"%s\"}",
                        type_str, value);
                } else {
                    snprintf(response, response_len,
                        "{\"status\":\"error\",\"cmd\":\"filter_add\","
                        "\"msg\":\"add failed: %d\"}", ret);
                }
            }
            filter_done:;
        }
        else {
            snprintf(response, response_len,
                "{\"status\":\"error\",\"cmd\":\"filter\",\"msg\":\"unknown action\"}");
        }
    }
    else if (strncmp(cmd, "LUA ", 4) == 0) {
        const char *action = cmd + 4;
        if (strncmp(action, "EXEC ", 5) == 0) {
            const char *script = action + 5;
            if (!lua_engine_is_ready()) {
                snprintf(response, response_len,
                    "{\"status\":\"error\",\"cmd\":\"lua_exec\","
                    "\"msg\":\"Lua engine not initialized\"}");
            } else {
                char lua_result[LUA_RESULT_MAX_LEN] = {0};
                int ret = lua_engine_exec(script, lua_result, sizeof(lua_result));
                if (ret == 0) {
                    snprintf(response, response_len,
                        "{\"status\":\"ok\",\"cmd\":\"lua_exec\","
                        "\"result\":\"%s\"}", lua_result);
                } else {
                    snprintf(response, response_len,
                        "{\"status\":\"error\",\"cmd\":\"lua_exec\","
                        "\"code\":%d,\"msg\":\"%s\"}",
                        ret, lua_result[0] ? lua_result : "exec failed");
                }
            }
        }
        else if (strcmp(action, "INIT") == 0) {
            int ret = lua_engine_init();
            if (ret == 0) {
                snprintf(response, response_len,
                    "{\"status\":\"ok\",\"cmd\":\"lua_init\"}");
            } else {
                snprintf(response, response_len,
                    "{\"status\":\"error\",\"cmd\":\"lua_init\","
                    "\"code\":%d}", ret);
            }
        }
        else if (strcmp(action, "DEINIT") == 0) {
            int ret = lua_engine_deinit();
            if (ret == 0) {
                snprintf(response, response_len,
                    "{\"status\":\"ok\",\"cmd\":\"lua_deinit\"}");
            } else {
                snprintf(response, response_len,
                    "{\"status\":\"error\",\"cmd\":\"lua_deinit\","
                    "\"code\":%d}", ret);
            }
        }
        else {
            snprintf(response, response_len,
                "{\"status\":\"error\",\"cmd\":\"lua\","
                "\"msg\":\"usage: LUA EXEC|INIT|DEINIT\"}");
        }
    }
    else if (strncmp(cmd, "SCRIPT ", 7) == 0) {
        const char *action = cmd + 7;
        if (strcmp(action, "BEGIN") == 0) {
            int ret = script_upload_begin();
            if (ret == 0) {
                snprintf(response, response_len,
                    "{\"status\":\"ok\",\"cmd\":\"script_begin\"}");
            } else {
                snprintf(response, response_len,
                    "{\"status\":\"error\",\"cmd\":\"script_begin\","
                    "\"code\":%d}", ret);
            }
        }
        else if (strncmp(action, "CHUNK ", 6) == 0) {
            /* SCRIPT CHUNK <hex_data> — validated decode (M-S3-8 fix:
             * previously non-hex chars decoded as 0, odd lengths dropped
             * the last nibble, and >512-byte payloads truncated silently) */
            const char *hex = action + 6;
            size_t hex_len = strlen(hex);
            uint8_t chunk_buf[512];
            uint16_t chunk_len = 0;
            bool hex_ok = (hex_len > 0) && (hex_len % 2 == 0) &&
                          (hex_len / 2 <= sizeof(chunk_buf));

            if (hex_ok) {
                for (size_t i = 0; i < hex_len; i += 2) {
                    int hi = s_hex_nibble(hex[i]);
                    int lo = s_hex_nibble(hex[i + 1]);
                    if (hi < 0 || lo < 0) {
                        hex_ok = false;
                        break;
                    }
                    chunk_buf[chunk_len++] = (uint8_t)((hi << 4) | lo);
                }
            }

            if (!hex_ok) {
                snprintf(response, response_len,
                    "{\"status\":\"error\",\"cmd\":\"script_chunk\","
                    "\"msg\":\"invalid hex: even length, 0-9a-f, max %u bytes\"}",
                    (unsigned)sizeof(chunk_buf));
            } else {
                int ret = script_upload_chunk(chunk_buf, chunk_len);
                if (ret == 0) {
                    snprintf(response, response_len,
                        "{\"status\":\"ok\",\"cmd\":\"script_chunk\","
                        "\"bytes\":%d}", chunk_len);
                } else {
                    snprintf(response, response_len,
                        "{\"status\":\"error\",\"cmd\":\"script_chunk\","
                        "\"code\":%d}", ret);
                }
            }
        }
        else if (strcmp(action, "END") == 0) {
            int ret = script_upload_end();
            if (ret == 0) {
                snprintf(response, response_len,
                    "{\"status\":\"ok\",\"cmd\":\"script_end\"}");
            } else {
                snprintf(response, response_len,
                    "{\"status\":\"error\",\"cmd\":\"script_end\","
                    "\"code\":%d}", ret);
            }
        }
        else if (strcmp(action, "RUN") == 0) {
            int ret = script_run();
            if (ret == 0) {
                snprintf(response, response_len,
                    "{\"status\":\"ok\",\"cmd\":\"script_run\"}");
            } else {
                snprintf(response, response_len,
                    "{\"status\":\"error\",\"cmd\":\"script_run\","
                    "\"code\":%d}", ret);
            }
        }
        else if (strcmp(action, "STOP") == 0) {
            script_stop();
            snprintf(response, response_len,
                "{\"status\":\"ok\",\"cmd\":\"script_stop\"}");
        }
        else if (strcmp(action, "STATUS") == 0) {
            snprintf(response, response_len,
                "{\"status\":\"ok\",\"cmd\":\"script_status\","
                "\"loaded\":%s,\"running\":%s}",
                script_is_loaded() ? "true" : "false",
                script_is_running() ? "true" : "false");
        }
        else {
            snprintf(response, response_len,
                "{\"status\":\"error\",\"cmd\":\"script\","
                "\"msg\":\"usage: SCRIPT RUN|STOP|STATUS\"}");
        }
    }
    else {
        snprintf(response, response_len,
            "{\"status\":\"error\",\"msg\":\"unknown command\"}");
    }
}

void app_main(void)
{
    printf("\n=== BLE Sniffer Dongle v0.3.0 ===\n");
    printf("Commands: STATUS, VERSION, SCAN START/STOP, FILTER ADD/CLEAR/LIST, LUA INIT/EXEC/DEINIT, SCRIPT RUN/STOP/STATUS\n\n");

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
    pipeline_set_filter(&s_filter_engine);

    /* Initialize Lua engine */
    ret = lua_engine_init();
    if (ret != 0) {
        printf("WARNING: Lua engine init failed (%d)\n", ret);
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
