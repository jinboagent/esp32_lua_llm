#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "filter_if.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * BLE Interface — NimBLE init, passive scan, scan pipeline, and the
 * optional central-role connection (F2.4, CONFIG_BLE_CONN_ENABLED)
 *
 * Error codes:
 *   BLE init:  -6 (not initialized), -7 (already initialized)
 *   BLE scan:  -401 (invalid param), -402 (null pointer),
 *              -406 (not initialized), -411 (invalid state)
 *   BLE conn:  -450 (invalid param), -452 (invalid state),
 *              -453 (not connected), -454 (discovery failed),
 *              -455 (connect timeout), -456 (no target configured)
 *              (-451 "not compiled in" is emitted by the CLI when the
 *               feature is excluded from the build)
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

/*
 * Register a raw-report tap (F2.4). The callback is invoked in the NimBLE
 * host task for EVERY received advertisement, BEFORE dedup — it must be
 * non-blocking and allocation-free (a 31-byte AD parse is ~µs). Passing
 * NULL unregisters. Used by the connection feature to auto-find the
 * target peer while a user scan is running.
 */
void ble_scan_set_tap(void (*tap)(const adv_report_raw_t *report));

/*
 * Pause/resume the discovery procedure without changing the user-visible
 * scanning state (F2.4 coexistence seam). The NimBLE host runs one GAP
 * procedure: ble_gap_connect() returns EBUSY while a discovery is active,
 * so the connection feature briefly pauses discovery around the connect
 * attempt and resumes it once the link exists. Pause while not scanning is
 * a no-op; DISC_COMPLETE does not restart discovery while paused.
 */
void ble_scan_pause(void);
void ble_scan_resume(void);

/* --- BLE Connection (F2.4, optional central role) ---------------------- */

#define BLE_CONN_PAYLOAD_MAX     253    /* ATT_MTU(256) - 3 opcode/handle */
#define BLE_CONN_QUEUE_DEPTH     8
#define BLE_CONN_POLL_MIN_MS     100
#define BLE_CONN_POLL_MAX_MS     10000
#define BLE_CONN_POLL_DEFAULT_MS 1000

/* Named error codes, module range -450..-459 (adopted from variant A in
 * the 2026-08-22 improvement pass — names over bare numbers). */
#define BLE_CONN_ERR_INVALID_PARAM (-450)
#define BLE_CONN_ERR_NOT_ENABLED   (-451)
#define BLE_CONN_ERR_INVALID_STATE (-452)
#define BLE_CONN_ERR_NOT_CONNECTED (-453)
#define BLE_CONN_ERR_DISCOVERY     (-454)
#define BLE_CONN_ERR_TIMEOUT       (-455)
#define BLE_CONN_ERR_NO_TARGET     (-456)
#define BLE_CONN_ERR_INTERRUPTED   (-457)  /* direct start aborted by Ctrl+C */

typedef enum {
    BLE_CONN_STATE_OFF = 0,      /* no connection activity                  */
    BLE_CONN_STATE_PEER_SEARCH,  /* auto-connect: waiting for target adv    */
    BLE_CONN_STATE_CONNECTING,   /* ble_gap_connect in flight               */
    BLE_CONN_STATE_DISCOVERING,  /* GATT service/char/CCCD discovery        */
    BLE_CONN_STATE_ACTIVE        /* subscribed (notify/indicate) or polling */
} ble_conn_state_t;

typedef struct {
    ble_conn_state_t state;
    uint8_t  peer_addr[6];        /* current/last peer                      */
    uint8_t  peer_addr_type;
    bool     subscribed;          /* CCCD written (notify/indicate mode)    */
    bool     polling;             /* poll fallback active                   */
    uint16_t mtu;                 /* negotiated ATT MTU                     */
    uint32_t poll_interval_ms;
    uint32_t connects;            /* links established                      */
    uint32_t disconnects;         /* links lost or closed                   */
    uint32_t rx_notify;           /* notifications received                 */
    uint32_t rx_read;             /* poll reads completed                   */
    uint32_t tx_lines;            /* JSON lines emitted to USB              */
    uint32_t dropped;             /* payloads dropped (queue full / TX)     */
    uint32_t errors;              /* procedure failures (discovery, CCCD)   */
} ble_conn_status_t;

/*
 * Connection lifecycle event, invoked (not from the CLI task) whenever a
 * link is established or torn down. Wired in main.c to the power hold so
 * the ble component never depends on power (no layering cycle).
 */
typedef void (*ble_conn_event_fn)(bool connected, const uint8_t *addr);

int  ble_conn_init(void);
void ble_conn_set_event_cb(ble_conn_event_fn cb);

/*
 * Set the target service UUID (and optionally the characteristic UUID).
 * Accepted forms: "180A" (16-bit), 8 hex digits (32-bit), or the canonical
 * 36-char 128-bit form. chr_uuid NULL = auto-pick (first notify, then
 * indicate, then readable characteristic in the service).
 * -450 bad UUID string; -452 a connection is active.
 */
int  ble_conn_set_target(const char *svc_uuid, const char *chr_uuid);

/*
 * Start a connection. With addr_str NULL: auto-connect — search (via the
 * scan tap while scanning, else an own discovery) for a peer advertising
 * the target service; returns 0 immediately (search observable via
 * ble_conn_get_status). With addr_str: direct connect ("AA:BB:..",
 * addr_type "public"|"random", default public; the tap auto-learns the
 * type when the address was seen advertising) — blocks until the link is
 * up or fails (-455 timeout / -454 discovery failure).
 * -450 bad address; -452 already connecting/active; -456 no target.
 */
int  ble_conn_start(const char *addr_str, const char *addr_type);

int  ble_conn_stop(void);
ble_conn_state_t ble_conn_get_state(void);
const char *ble_conn_state_name(ble_conn_state_t st);  /* "off"/"peer_search"/... */
bool ble_conn_is_active(void);
void ble_conn_get_status(ble_conn_status_t *out);
int  ble_conn_set_poll_interval(uint32_t ms);

/* --- Scan Pipeline (F2.3) --- */
int  pipeline_init(void);
int  pipeline_start(void);
int  pipeline_stop(void);
void pipeline_set_filter(filter_engine_t *eng);
int  pipeline_get_stats(pipeline_stats_t *stats);

#ifdef __cplusplus
}
#endif
