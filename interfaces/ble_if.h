#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "filter_if.h"

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
#define BLE_DEDUP_TABLE_SIZE    128
#define BLE_DEDUP_WINDOW_MS     1000

/* Raw advertisement report from BLE scan */
typedef struct {
    uint8_t  addr[6];
    uint8_t  addr_type;     /* 0=public, 1=random */
    int8_t   rssi;
    uint8_t  adv_data_len;  /* 0..31 */
    uint8_t  adv_data[BLE_ADV_DATA_MAX_LEN];
    uint32_t ts_ms;         /* N1 fix: reception time, ms since boot */
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

/*
 * Get the number of advertisement reports dropped because the scan queue
 * was full. Reset by ble_scan_start().
 */
uint32_t ble_scan_get_drop_count(void);

/* --- Scan Pipeline (F2.3) --- */
int  pipeline_init(void);
int  pipeline_start(void);
int  pipeline_stop(void);
void pipeline_set_filter(filter_engine_t *eng);
int  pipeline_get_stats(pipeline_stats_t *stats);

/* --- BLE Connection (F2.4) ---
 * Central-role connection to one peer; GATT payloads are re-streamed as
 * "src":"conn" JSON lines on the same USB stream. Compiled only under
 * CONFIG_BLE_CONN_ENABLED; the symbols exist in that build only.
 * Error range -450..-459.
 */
#define BLE_CONN_PAYLOAD_MAX_LEN  256

#define BLE_CONN_ERR_INVALID_PARAM  (-450)
#define BLE_CONN_ERR_NOT_ENABLED    (-451)
#define BLE_CONN_ERR_INVALID_STATE  (-452)
#define BLE_CONN_ERR_NOT_CONNECTED  (-453)
#define BLE_CONN_ERR_DISCOVERY      (-454)
#define BLE_CONN_ERR_TIMEOUT        (-455)
#define BLE_CONN_ERR_NO_TARGET      (-456)

typedef enum {
    BLE_CONN_STATE_IDLE = 0,
    BLE_CONN_STATE_PEER_SEARCH,
    BLE_CONN_STATE_CONNECTING,
    BLE_CONN_STATE_DISCOVERING,
    BLE_CONN_STATE_ACTIVE,
} ble_conn_state_t;

typedef struct {
    ble_conn_state_t state;
    uint8_t  addr[6];
    uint8_t  addr_type;     /* peer address type when known */
    bool     connected;
    bool     notify_mode;   /* true = notify/indicate, false = poll */
    uint32_t rx_lines;
    uint32_t drops;         /* payload queue full (drop-newest) */
    uint32_t poll_ms;
} ble_conn_info_t;

#define BLE_CONN_EVT_CONNECTED    1
#define BLE_CONN_EVT_DISCONNECTED 2
typedef void (*ble_conn_event_cb_t)(int event);

int  ble_conn_init(void);
/* char_uuid may be NULL (first characteristic with notify/read in svc). */
int  ble_conn_set_target(const char *svc_uuid, const char *char_uuid);
/* addr NULL = auto-connect by target service UUID; addr_type -1 =
 * auto-learn from scan tap (fallback public), 0 = public, 1 = random. */
int  ble_conn_start(const char *addr, int addr_type);
int  ble_conn_stop(void);
bool ble_conn_is_active(void);
int  ble_conn_set_poll_interval(uint32_t ms);
int  ble_conn_get_info(ble_conn_info_t *info);
int  ble_conn_set_event_cb(ble_conn_event_cb_t cb);

/* Raw-report tap (F2.4): invoked for every DISC event before dedup,
 * from the NimBLE host task — the callback must stay fast (it may parse
 * the raw adv data itself to see 128-bit UUIDs). NULL disables. */
void ble_scan_set_tap(void (*cb)(const adv_report_raw_t *report));

#ifdef __cplusplus
}
#endif
