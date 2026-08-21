#include "ble_if.h"
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "esp_timer.h"

/* Dedup table entry */
typedef struct {
    uint8_t  addr[6];
    int64_t  last_seen_us;
} dedup_entry_t;

static QueueHandle_t s_scan_queue = NULL;
static atomic_bool s_scanning = false;  /* B-S2-2 fix: atomic access */
static atomic_uint s_queue_drop_count = 0;  /* M-S2-4 fix: observability */
static void (*s_tap)(const adv_report_raw_t *) = NULL;  /* F2.4 */

/* Protects s_dedup against concurrent access from the NimBLE host task
 * (GAP event handler) and the CLI task (memset in ble_scan_start) — B-S2-3
 * fix. Task-level critical section; the table is only touched for a few
 * microseconds per advertisement. */
static portMUX_TYPE s_dedup_mux = portMUX_INITIALIZER_UNLOCKED;

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
    /* B7 fix: esp_timer_get_time() starts at 0 at boot, which collides
     * with the 0 = "empty slot" sentinel below. Shift by 1 µs so a
     * recorded timestamp can never be mistaken for an empty slot. */
    int64_t now_us = esp_timer_get_time() + 1;
    uint8_t idx = s_dedup_hash(addr);
    bool duplicate = false;
    bool recorded = false;

    portENTER_CRITICAL(&s_dedup_mux);

    /* Linear probing — check up to 4 slots (B-S2-4 fix) */
    for (int probe = 0; probe < 4; probe++) {
        uint8_t slot = (idx + probe) % BLE_DEDUP_TABLE_SIZE;

        if (memcmp(s_dedup[slot].addr, addr, 6) == 0) {
            /* Found matching entry — check timestamp */
            int64_t elapsed = now_us - s_dedup[slot].last_seen_us;
            if (elapsed < (int64_t)BLE_DEDUP_WINDOW_MS * 1000) {
                duplicate = true; /* duplicate */
            } else {
                /* Entry expired — reuse it */
                s_dedup[slot].last_seen_us = now_us;
            }
            recorded = true;
            break;
        }

        /* Empty slot (never used or expired) — claim it */
        if (s_dedup[slot].last_seen_us == 0) {
            memcpy(s_dedup[slot].addr, addr, 6);
            s_dedup[slot].last_seen_us = now_us;
            recorded = true;
            break;
        }
    }

    if (!recorded) {
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
    }

    portEXIT_CRITICAL(&s_dedup_mux);
    return duplicate;
}

static int s_gap_event_handler(struct ble_gap_event *event, void *arg);

/* Build discovery parameters from the current interval/window and start a
 * NimBLE discovery window. Shared by ble_scan_start and the DISC_COMPLETE
 * restart (N2 fix). Returns 0 on success, NimBLE error code otherwise. */
static int s_start_discovery(void)
{
    /* Configure scan parameters.
     * Clamp converted values to the BLE spec range for scan interval/window
     * (0x0004..0x4000 in 0.625 ms units) as defense in depth (L-S2-2 fix). */
    uint16_t itvl = (uint16_t)((s_interval_ms * 1000) / 625);
    uint16_t window = (uint16_t)((s_window_ms * 1000) / 625);
    if (itvl < 0x0004) itvl = 0x0004;
    if (itvl > 0x4000) itvl = 0x4000;
    if (window < 0x0004) window = 0x0004;
    if (window > 0x4000) window = 0x4000;
    if (window > itvl) window = itvl;

    struct ble_gap_disc_params params = {
        .itvl = itvl,
        .window = window,
        .passive = 1,
        .filter_duplicates = 0,
    };

    /* N2 fix (final): scan forever instead of NimBLE's default 10.24 s
     * windows. duration=0 maps to the default window and forces a
     * DISC_COMPLETE restart cycle, whose first event can be lost when a
     * scan starts right after boot. BLE_HS_FOREVER gives one continuous
     * discovery; the DISC_COMPLETE handler below stays as a fallback. */
    return ble_gap_disc(BLE_OWN_ADDR_PUBLIC, BLE_HS_FOREVER, &params,
                        s_gap_event_handler, NULL);
}

static int s_gap_event_handler(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_DISC: {
        struct ble_gap_disc_desc *disc = &event->disc;

        /* Build the raw report first: the F2.4 tap sees every report,
         * including ones dedup drops (peer search needs raw visibility). */
        adv_report_raw_t report;
        memcpy(report.addr, disc->addr.val, 6);
        report.addr_type = disc->addr.type;
        report.rssi = disc->rssi;
        /* N1 fix: timestamp at reception (ms since boot) — was never set,
         * so every JSON line carried "ts":0 */
        report.ts_ms = (uint32_t)(esp_timer_get_time() / 1000);

        uint8_t data_len = disc->length_data;
        if (data_len > BLE_ADV_DATA_MAX_LEN) {
            data_len = BLE_ADV_DATA_MAX_LEN;
        }
        report.adv_data_len = data_len;
        memcpy(report.adv_data, disc->data, data_len);

        /* F2.4 raw-report tap (no-op when unset); runs in the NimBLE
         * host task — the callback must stay fast. */
        if (s_tap != NULL) {
            s_tap(&report);
        }

        /* Dedup check */
        if (s_dedup_check(disc->addr.val)) {
            return 0; /* skip duplicate */
        }

        /* Non-blocking enqueue — drop if queue full (counted, M-S2-4 fix) */
        if (xQueueSend(s_scan_queue, &report, 0) != pdTRUE) {
            atomic_fetch_add(&s_queue_drop_count, 1);
        }
        break;
    }

    case BLE_GAP_EVENT_DISC_COMPLETE:
        /* N2 fix: NimBLE discovery runs in finite-duration windows. A
         * continuous sniffer must start the next window here — the old
         * code just marked the scan over, so every scan died after ~10 s.
         * ble_scan_stop clears s_scanning before cancelling, so a
         * user-requested stop never restarts. */
        if (atomic_load(&s_scanning)) {
            int rc = s_start_discovery();
            if (rc != 0) {
                atomic_store(&s_scanning, false);
                printf("BLE: scan restart failed (%d) — scanning stopped\n", rc);
            }
        }
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
    atomic_store(&s_queue_drop_count, 0);

    /* Clear dedup table under the same lock that protects it from the GAP
     * event handler — a handler callback from the previous scan may still
     * be in flight in the NimBLE host task (B-S2-3 fix) */
    portENTER_CRITICAL(&s_dedup_mux);
    memset(s_dedup, 0, sizeof(s_dedup));
    portEXIT_CRITICAL(&s_dedup_mux);

    int rc = s_start_discovery();
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

    /* N2 fix: clear the flag BEFORE cancelling — the cancel triggers
     * BLE_GAP_EVENT_DISC_COMPLETE, and the handler only restarts
     * discovery while s_scanning is still true. */
    atomic_store(&s_scanning, false);

    int rc = ble_gap_disc_cancel();
    if (rc != 0 && rc != BLE_HS_EALREADY) {
        printf("BLE: scan stop failed: %d\n", rc);
        return -1;
    }

    /* Drain stale reports so a consumer that reads after stop never sees
     * data from the stopped scan (L4 fix) */
    if (s_scan_queue != NULL) {
        xQueueReset(s_scan_queue);
    }

    printf("BLE: scan stopped\n");
    return 0;
}

bool ble_scan_is_active(void)
{
    return atomic_load(&s_scanning);
}

uint32_t ble_scan_get_drop_count(void)
{
    return atomic_load(&s_queue_drop_count);
}

int ble_scan_set_params(uint32_t interval_ms, uint32_t window_ms)
{
    if (atomic_load(&s_scanning)) {
        return -411; /* parameters apply at next scan start (L-S2-3 fix) */
    }
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

void ble_scan_set_tap(void (*cb)(const adv_report_raw_t *))
{
    s_tap = cb;  /* F2.4: single writer at boot (ble_conn_init) */
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
