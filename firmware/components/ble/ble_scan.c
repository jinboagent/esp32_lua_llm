#include "ble_if.h"
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "host/ble_gap.h"
#include "esp_timer.h"

/* Dedup table entry */
typedef struct {
    uint8_t  addr[6];
    int64_t  last_seen_us;
} dedup_entry_t;

static QueueHandle_t s_scan_queue = NULL;
static atomic_bool s_scanning = false;  /* B-S2-2 fix: atomic access */

/* Scan parameters (NimBLE uses 0.625ms units) */
static uint32_t s_interval_ms = 100;
static uint32_t s_window_ms = 50;

/* Dedup table */
static dedup_entry_t s_dedup[BLE_DEDUP_TABLE_SIZE];

/* FNV-1a hash — much better distribution than XOR (B-S2-4 fix) */
static uint8_t s_dedup_hash(const uint8_t addr[6])
{
    uint32_t h = 2166136261u;
    for (int i = 0; i < 6; i++) {
        h ^= addr[i];
        h *= 16777619u;
    }
    return (uint8_t)(h % BLE_DEDUP_TABLE_SIZE);
}

static bool s_dedup_check(const uint8_t addr[6])
{
    int64_t now_us = esp_timer_get_time();
    uint8_t idx = s_dedup_hash(addr);

    /* Linear probing — check up to 4 slots (B-S2-4 fix) */
    for (int probe = 0; probe < 4; probe++) {
        uint8_t slot = (idx + probe) % BLE_DEDUP_TABLE_SIZE;

        if (memcmp(s_dedup[slot].addr, addr, 6) == 0) {
            /* Found matching entry — check timestamp */
            int64_t elapsed = now_us - s_dedup[slot].last_seen_us;
            if (elapsed < (int64_t)BLE_DEDUP_WINDOW_MS * 1000) {
                return true; /* duplicate */
            }
            /* Entry expired — reuse it */
            s_dedup[slot].last_seen_us = now_us;
            return false;
        }

        /* Empty slot (never used or expired) — claim it */
        if (s_dedup[slot].last_seen_us == 0) {
            memcpy(s_dedup[slot].addr, addr, 6);
            s_dedup[slot].last_seen_us = now_us;
            return false;
        }
    }

    /* All probed slots occupied by other devices — evict oldest */
    uint8_t oldest_slot = idx;
    int64_t oldest_time = s_dedup[idx].last_seen_us;
    for (int probe = 1; probe < 4; probe++) {
        uint8_t slot = (idx + probe) % BLE_DEDUP_TABLE_SIZE;
        if (s_dedup[slot].last_seen_us < oldest_time) {
            oldest_time = s_dedup[slot].last_seen_us;
            oldest_slot = slot;
        }
    }
    memcpy(s_dedup[oldest_slot].addr, addr, 6);
    s_dedup[oldest_slot].last_seen_us = now_us;
    return false;
}

static int s_gap_event_handler(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_DISC: {
        struct ble_gap_disc_desc *disc = &event->disc;

        /* Dedup check */
        if (s_dedup_check(disc->addr.val)) {
            return 0; /* skip duplicate */
        }

        /* Build raw report */
        adv_report_raw_t report;
        memcpy(report.addr, disc->addr.val, 6);
        report.addr_type = disc->addr.type;
        report.rssi = disc->rssi;

        uint8_t data_len = disc->length_data;
        if (data_len > BLE_ADV_DATA_MAX_LEN) {
            data_len = BLE_ADV_DATA_MAX_LEN;
        }
        report.adv_data_len = data_len;
        memcpy(report.adv_data, disc->data, data_len);

        /* Non-blocking enqueue — drop if queue full */
        xQueueSend(s_scan_queue, &report, 0);
        break;
    }

    case BLE_GAP_EVENT_DISC_COMPLETE:
        atomic_store(&s_scanning, false);
        break;

    default:
        break;
    }

    return 0;
}

int ble_scan_start(void)
{
    if (!ble_is_ready()) {
        return -406;
    }
    if (atomic_load(&s_scanning)) {
        return -411;
    }

    /* Create queue if not exists, otherwise drain stale entries */
    if (s_scan_queue == NULL) {
        s_scan_queue = xQueueCreate(BLE_SCAN_QUEUE_DEPTH, sizeof(adv_report_raw_t));
        if (s_scan_queue == NULL) {
            return -1;
        }
    } else {
        xQueueReset(s_scan_queue);
    }

    /* Clear dedup table */
    memset(s_dedup, 0, sizeof(s_dedup));

    /* Configure scan parameters */
    struct ble_gap_disc_params params = {
        .itvl = (s_interval_ms * 1000) / 625,
        .window = (s_window_ms * 1000) / 625,
        .passive = 1,
        .filter_duplicates = 0,
    };

    int rc = ble_gap_disc(BLE_OWN_ADDR_PUBLIC, 0, &params, s_gap_event_handler, NULL);
    if (rc != 0) {
        printf("BLE: scan start failed: %d\n", rc);
        return -1;
    }

    atomic_store(&s_scanning, true);
    printf("BLE: passive scan started (interval=%lums, window=%lums)\n",
           (unsigned long)s_interval_ms, (unsigned long)s_window_ms);
    return 0;
}

int ble_scan_stop(void)
{
    if (!atomic_load(&s_scanning)) {
        return -411;
    }

    int rc = ble_gap_disc_cancel();
    if (rc != 0 && rc != BLE_HS_EALREADY) {
        printf("BLE: scan stop failed: %d\n", rc);
        return -1;
    }

    atomic_store(&s_scanning, false);
    printf("BLE: scan stopped\n");
    return 0;
}

bool ble_scan_is_active(void)
{
    return atomic_load(&s_scanning);
}

int ble_scan_set_params(uint32_t interval_ms, uint32_t window_ms)
{
    if (interval_ms < 10 || interval_ms > 10240) {
        return -401;
    }
    if (window_ms < 10 || window_ms > 10240) {
        return -401;
    }
    if (window_ms > interval_ms) {
        return -401;
    }

    s_interval_ms = interval_ms;
    s_window_ms = window_ms;
    return 0;
}

int ble_scan_get_report(adv_report_raw_t *out, uint32_t timeout_ms)
{
    if (out == NULL) {
        return -402;
    }
    if (s_scan_queue == NULL) {
        return -406;
    }

    TickType_t ticks = (timeout_ms == 0) ? 0 : pdMS_TO_TICKS(timeout_ms);
    if (xQueueReceive(s_scan_queue, out, ticks) == pdTRUE) {
        return 0;
    }
    return -5; /* timeout */
}
