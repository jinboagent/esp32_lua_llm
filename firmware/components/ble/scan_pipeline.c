#include "ble_if.h"
#include "proto_if.h"
#include "json_if.h"
#include "filter_if.h"
#include "usb_if.h"
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

static void s_pipeline_task_func(void *param)
{
    adv_report_raw_t raw;
    proto_adv_report_t parsed;
    char json_buf[JSON_LINE_MAX_LEN];
    uint16_t json_len = 0;

    while (s_running) {
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

        /* 4. Encode as JSON */
        ret = json_encode_adv(&parsed, json_buf, sizeof(json_buf), &json_len);
        if (ret != 0) {
            s_stats.encode_errors++;
            continue;
        }

        /* 5. Output via USB CDC */
        ret = usb_console_send_json(json_buf);
        if (ret == 0) {
            s_stats.total_output++;
        }
    }

    /* Task is stopping — clean up */
    vTaskDelete(NULL);
    s_pipeline_task = NULL;
}

int pipeline_init(void)
{
    if (s_pipeline_task != NULL) {
        return -811; /* already initialized */
    }

    memset(&s_stats, 0, sizeof(s_stats));
    s_running = false;
    s_filter_engine = NULL;

    /* Create task in suspended state (not running yet) */
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

    /* Suspend until pipeline_start() is called */
    vTaskSuspend(s_pipeline_task);
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
    vTaskResume(s_pipeline_task);
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
    /* Task will exit on its own after current iteration */
    printf("Pipeline: stopped (received=%lu, filtered=%lu, output=%lu, "
           "parse_err=%lu, encode_err=%lu)\n",
           (unsigned long)s_stats.total_received,
           (unsigned long)s_stats.total_filtered,
           (unsigned long)s_stats.total_output,
           (unsigned long)s_stats.parse_errors,
           (unsigned long)s_stats.encode_errors);
    return 0;
}

int pipeline_get_stats(pipeline_stats_t *stats)
{
    if (stats == NULL) {
        return -802;
    }
    *stats = s_stats;
    return 0;
}
