#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * BLE Interface — NimBLE init, passive scan, and scan pipeline
 *
 * Error codes:
 *   BLE init:  -6 (not initialized), -7 (already initialized)
 *   BLE scan:  -401 (invalid param), -402 (null pointer),
 *              -406 (not initialized), -411 (invalid state)
 *   Pipeline:  -802 (null pointer), -806 (not initialized),
 *              -811 (invalid state)
 */

#define BLE_ADV_DATA_MAX_LEN    31
#define BLE_SCAN_QUEUE_DEPTH    32
#define BLE_DEDUP_TABLE_SIZE    64
#define BLE_DEDUP_WINDOW_MS     1000

/* Raw advertisement report from BLE scan */
typedef struct {
    uint8_t  addr[6];
    uint8_t  addr_type;     /* 0=public, 1=random */
    int8_t   rssi;
    uint8_t  adv_data_len;  /* 0..31 */
    uint8_t  adv_data[BLE_ADV_DATA_MAX_LEN];
} adv_report_raw_t;

/* Pipeline statistics */
typedef struct {
    uint32_t total_received;
    uint32_t total_filtered;
    uint32_t total_output;
    uint32_t parse_errors;
    uint32_t encode_errors;
} pipeline_stats_t;

/* --- NimBLE Init (F2.1) --- */
int  ble_init(void);
int  ble_deinit(void);
bool ble_is_ready(void);

/* --- BLE Scan (F2.2) --- */
int  ble_scan_start(void);
int  ble_scan_stop(void);
bool ble_scan_is_active(void);
int  ble_scan_set_params(uint32_t interval_ms, uint32_t window_ms);
int  ble_scan_get_report(adv_report_raw_t *out, uint32_t timeout_ms);

/* --- Scan Pipeline (F2.3) --- */
int  pipeline_init(void);
int  pipeline_start(void);
int  pipeline_stop(void);
int  pipeline_get_stats(pipeline_stats_t *stats);

#ifdef __cplusplus
}
#endif
