#include "ble_if.h"
#include "proto_if.h"
#include "json_if.h"
#include "filter_if.h"
#include "usb_if.h"
#include "lua_if.h"
#include "script_if.h"
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define PIPELINE_TASK_STACK  4096
#define PIPELINE_TASK_PRIO   2
#define PIPELINE_QUEUE_TIMEOUT_MS  100

static TaskHandle_t s_pipeline_task = NULL;
static bool s_running = false;
static pipeline_stats_t s_stats;

/* Optional filter engine (set externally, not owned by pipeline) */
static filter_engine_t *s_filter_engine = NULL;

static void s_format_addr(const uint8_t *addr, char *buf, uint8_t buf_len)
{
    snprintf(buf, buf_len, "%02X:%02X:%02X:%02X:%02X:%02X",
             addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);
}

static void s_pipeline_task_func(void *param)
{
    adv_report_raw_t raw;
    proto_adv_report_t parsed;
    char json_buf[JSON_LINE_MAX_LEN];
    char addr_str[18]; /* "AA:BB:CC:DD:EE:FF\0" */
    uint16_t json_len = 0;

    for (;;) {
        if (!s_running) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        /* 1. Get raw report from BLE scan queue */
        int ret = ble_scan_get_report(&raw, PIPELINE_QUEUE_TIMEOUT_MS);
        if (ret != 0) {
            continue; /* timeout or error, try again */
        }

        s_stats.total_received++;

        /* 2. Parse AD structures */
        ret = proto_parse_adv_data(raw.adv_data, raw.adv_data_len, &parsed);
        if (ret != 0) {
            s_stats.parse_errors++;
            continue;
        }

        /* Copy address info from raw report */
        memcpy(parsed.addr, raw.addr, 6);
        parsed.addr_type = raw.addr_type;
        parsed.rssi = raw.rssi;

        /* 3. Filter */
        if (s_filter_engine != NULL) {
            if (!filter_evaluate(s_filter_engine, &parsed)) {
                s_stats.total_filtered++;
                continue;
            }
        }

        /* 3b. Lua on_adv hook */
        s_format_addr(parsed.addr, addr_str, sizeof(addr_str));
        if (script_is_running() && lua_engine_has_func("on_adv") == 1) {
            int hook_ret = lua_engine_call_on_adv("on_adv", addr_str,
                parsed.rssi, parsed.has_name ? parsed.name : NULL);
            if (hook_ret == 0) {
                /* Script suppressed this advertisement */
                s_stats.total_filtered++;
                continue;
            }
            /* hook_ret < 0 means error or no function — fall through to default */
        }

        /* 4. Encode as JSON */
        ret = json_encode_adv(&parsed, json_buf, sizeof(json_buf), &json_len);
        if (ret != 0) {
            s_stats.encode_errors++;
            continue;
        }

        /* 4b. Lua transform hook */
        if (script_is_running() && lua_engine_has_func("transform") == 1) {
            char transform_buf[JSON_LINE_MAX_LEN];
            int hook_ret = lua_engine_call_transform("transform", addr_str,
                json_buf, transform_buf, sizeof(transform_buf));
            if (hook_ret == 0 && transform_buf[0] != '\0') {
                /* Use transformed output */
                ret = usb_console_send_json(transform_buf);
                if (ret == 0) {
                    s_stats.total_output++;
                }
                continue;
            }
            /* hook_ret < 0 means error — fall through to default JSON */
        }

        /* 5. Output via USB CDC */
        ret = usb_console_send_json(json_buf);
        if (ret == 0) {
            s_stats.total_output++;
        }
    }

    /* Safety: FreeRTOS tasks must never return */
    vTaskDelete(NULL);
}

int pipeline_init(void)
{
    if (s_pipeline_task != NULL) {
        return -811; /* already initialized */
    }

    memset(&s_stats, 0, sizeof(s_stats));
    s_running = false;
    s_filter_engine = NULL;

    /* Create task — runs forever, idles when s_running == false */
    BaseType_t ret = xTaskCreatePinnedToCore(
        s_pipeline_task_func,
        "pipeline",
        PIPELINE_TASK_STACK,
        NULL,
        PIPELINE_TASK_PRIO,
        &s_pipeline_task,
        0  /* core 0 */
    );

    if (ret != pdPASS) {
        s_pipeline_task = NULL;
        return -1;
    }

    return 0;
}

int pipeline_start(void)
{
    if (s_pipeline_task == NULL) {
        return -806;
    }
    if (s_running) {
        return -811;
    }

    s_running = true;
    printf("Pipeline: started\n");
    return 0;
}

int pipeline_stop(void)
{
    if (s_pipeline_task == NULL) {
        return -806;
    }
    if (!s_running) {
        return -811;
    }

    s_running = false;

    printf("Pipeline: stopped (received=%lu, filtered=%lu, output=%lu, "
           "parse_err=%lu, encode_err=%lu)\n",
           (unsigned long)s_stats.total_received,
           (unsigned long)s_stats.total_filtered,
           (unsigned long)s_stats.total_output,
           (unsigned long)s_stats.parse_errors,
           (unsigned long)s_stats.encode_errors);
    return 0;
}

void pipeline_set_filter(filter_engine_t *eng)
{
    s_filter_engine = eng;
}

int pipeline_get_stats(pipeline_stats_t *stats)
{
    if (stats == NULL) {
        return -802;
    }
    *stats = s_stats;
    return 0;
}
