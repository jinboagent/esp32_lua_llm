/*
 * BLE Connection — optional central role GATT client (F2.4).
 *
 * Compiled only when CONFIG_BLE_CONN_ENABLED=y (CMake gates the source).
 * Connects to ONE peer (auto-connect by advertised service UUID, or by
 * direct address), subscribes to a notify/indicate characteristic with a
 * poll fallback, and re-streams payloads as "src":"conn" JSON lines on
 * the same USB stream as advertisements. Connection data deliberately
 * does NOT pass the Lua hooks (proposal §2.3); the pipeline stays
 * adv-only — generalizing it is the recorded future refactor trigger.
 *
 * Concurrency model (review A2/A6):
 *  - All shared state lives behind s_mux, owned by this module. Nothing
 *    else takes it and this module never borrows another module's lock.
 *  - NimBLE GAP/GATT callbacks run in the host task; they only copy data
 *    and push to queues — never format JSON, never touch USB.
 *  - One worker task owns the USB side and the blocking connect sequence.
 *    A slow or absent USB reader can never stall the BLE stack; the rx
 *    queue (depth 8) absorbs bursts, drops are counted (never blocked).
 *  - The NimBLE host runs one GAP procedure at a time: the user scan's
 *    discovery is paused around the connect attempt (ble_scan_pause) and
 *    resumed once the link exists or the attempt fails — the user never
 *    loses the scan for longer than the connect handshake.
 *
 * Zero dynamic allocation outside FreeRTOS queue/task creation (same
 * policy as the scan/pipeline siblings).
 */

#include "ble_if.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_hs_adv.h"
#include "os/os_mbuf.h"
#include "json_if.h"
#include "usb_if.h"

#define CONN_WORKER_STACK      4096
#define CONN_WORKER_PRIO       2
#define CONN_OP_QUEUE_DEPTH    4
#define CONN_CONNECT_TIMEOUT_MS 6000
#define CONN_CONNECT_RETRIES   5
#define CONN_CONNECT_RETRY_DELAY_MS 30

/* Address-type marker for the "find the peer first" op (auto-connect). */
#define CONN_ADDR_TYPE_SEARCH  0xFE

/* CCCD values (BT spec) */
#define CONN_CCCD_NOTIFY       0x0001
#define CONN_CCCD_INDICATE     0x0002
#define CONN_CCCD_UUID16       0x2902

typedef struct {
    uint8_t type;              /* CONN_OP_* */
    uint8_t addr[6];
    uint8_t addr_type;
} conn_op_t;

enum {
    CONN_OP_CONNECT = 1,       /* addr valid; addr_type may be SEARCH */
    CONN_OP_STOP,              /* finalize from the worker */
    CONN_OP_DISC_NOTICE,       /* emit a disconnect notice line */
};

typedef struct {
    uint8_t  addr[6];
    uint32_t ts_ms;
    uint16_t len;
    bool     from_poll;
    uint8_t  data[BLE_CONN_PAYLOAD_MAX];
} conn_rx_t;

/* ---- Shared state ------------------------------------------------------ */

static SemaphoreHandle_t s_mux = NULL;      /* guards everything below    */
static QueueHandle_t s_op_q = NULL;
static QueueHandle_t s_rx_q = NULL;
static SemaphoreHandle_t s_start_sem = NULL; /* direct-start completion    */
static TaskHandle_t s_worker = NULL;
static ble_conn_event_fn s_event_cb = NULL; /* set once at init           */

static ble_conn_state_t s_state = BLE_CONN_STATE_OFF;
static ble_conn_status_t s_status;
static ble_uuid_any_t s_svc_uuid;           /* target service             */
static ble_uuid_any_t s_chr_uuid;           /* target characteristic      */
static bool s_have_svc = false;
static bool s_have_chr = false;

static uint16_t s_conn_handle = 0xFFFF;
static uint16_t s_svc_start = 0, s_svc_end = 0;
static bool s_svc_found = false;
static uint16_t s_chr_def = 0, s_chr_val = 0;
static uint8_t s_chr_props = 0;
static bool s_chr_found = false;
static uint16_t s_cccd_handle = 0;

static bool s_own_disc = false;             /* our own discovery running  */
static bool s_scan_paused = false;          /* WE paused the user scan    */
static int  s_fail_code = 0;                /* direct-start failure code  */
static bool s_direct_wait = false;          /* CLI blocked on start_sem   */

/* Address-type auto-learn (review A3): filled by the tap when the
 * directly-addressed peer is seen advertising. */
static uint8_t s_learn_addr[6];
static uint8_t s_learned_type = 0xFF;
static bool s_learning = false;

static uint32_t s_next_poll_ms = 0;

/* ---- Forward decls ----------------------------------------------------- */

static int  s_gap_event(struct ble_gap_event *event, void *arg);
static int  s_svc_disc_cb(uint16_t conn, const struct ble_gatt_error *error,
                          const struct ble_gatt_svc *svc, void *arg);
static int  s_chr_disc_cb(uint16_t conn, const struct ble_gatt_error *error,
                          const struct ble_gatt_chr *chr, void *arg);
static int  s_dsc_disc_cb(uint16_t conn, const struct ble_gatt_error *error,
                          uint16_t chr_val_handle,
                          const struct ble_gatt_dsc *dsc, void *arg);
static int  s_cccd_write_cb(uint16_t conn, const struct ble_gatt_error *error,
                            struct ble_gatt_attr *attr, void *arg);
static int  s_read_cb(uint16_t conn, const struct ble_gatt_error *error,
                      struct ble_gatt_attr *attr, void *arg);
static void s_tap(const adv_report_raw_t *report);
static void s_worker_task(void *param);

/* ---- Helpers (host/worker/CLI safe) ------------------------------------ */

static void s_format_addr(const uint8_t addr[6], char *buf, size_t len)
{
    snprintf(buf, len, "%02X:%02X:%02X:%02X:%02X:%02X",
             addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);
}

static const char *s_state_name(ble_conn_state_t st)
{
    switch (st) {
        case BLE_CONN_STATE_PEER_SEARCH:  return "peer_search";
        case BLE_CONN_STATE_CONNECTING:   return "connecting";
        case BLE_CONN_STATE_DISCOVERING:  return "discovering";
        case BLE_CONN_STATE_ACTIVE:       return "active";
        default:                          return "off";
    }
}

const char *ble_conn_state_name(ble_conn_state_t st)
{
    return s_state_name(st);
}

/* "AA:BB:CC:DD:EE:FF" -> bytes. Returns 0 ok, -450 malformed. */
static int s_parse_addr(const char *str, uint8_t out[6])
{
    if (str == NULL || strlen(str) != 17) return -450;
    for (int i = 0; i < 6; i++) {
        int byte = 0;
        for (int h = 0; h < 2; h++) {
            char c = str[i * 3 + h];
            int v;
            if (c >= '0' && c <= '9') v = c - '0';
            else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
            else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
            else return -450;
            byte = (byte << 4) | v;
        }
        if (i < 5 && str[i * 3 + 2] != ':') return -450;
        out[i] = (uint8_t)byte;
    }
    return 0;
}

static int s_hex_nibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/*
 * UUID string -> ble_uuid_any_t. Accepts "180A" (16-bit), 8 hex digits
 * (32-bit) and the canonical 36-char 128-bit form. NimBLE stores UUIDs
 * little-endian (ATT order), so the parsed big-endian bytes are reversed
 * before ble_uuid_init_from_buf.
 */
static int s_parse_uuid(const char *str, ble_uuid_any_t *uuid)
{
    if (str == NULL || uuid == NULL) return -450;

    uint8_t msb[16];
    int n = 0;
    for (const char *p = str; *p != '\0'; p++) {
        if (*p == '-') continue;
        int hi = s_hex_nibble(*p);
        if (hi < 0 || n >= 32) return -450;
        p++;
        int lo = s_hex_nibble(*p);
        if (lo < 0) return -450;
        msb[n / 2] = (uint8_t)((hi << 4) | lo);
        n += 2;
    }
    if (n != 4 && n != 8 && n != 32) return -450;

    uint8_t le[16];
    for (int i = 0; i < n / 2; i++) {
        le[i] = msb[n / 2 - 1 - i];
    }
    if (ble_uuid_init_from_buf(uuid, le, n / 2) != 0) {
        return -450;
    }
    return 0;
}

/* Does this advertisement advertise the target service UUID? (review A4:
 * parses ALL service-UUID widths itself — the proto layer only surfaces
 * UUID16. Runs in the host task, so: bounded, no allocation.) */
static bool s_adv_matches_target(const uint8_t *data, uint8_t len)
{
    struct ble_hs_adv_fields fields;
    int rc = ble_hs_adv_parse_fields(&fields, data, len);
    if (rc != 0) return false;

    for (int i = 0; i < fields.num_uuids16; i++) {
        if (ble_uuid_cmp(&fields.uuids16[i].u, &s_svc_uuid.u) == 0) return true;
    }
    for (int i = 0; i < fields.num_uuids32; i++) {
        if (ble_uuid_cmp(&fields.uuids32[i].u, &s_svc_uuid.u) == 0) return true;
    }
    for (int i = 0; i < fields.num_uuids128; i++) {
        if (ble_uuid_cmp(&fields.uuids128[i].u, &s_svc_uuid.u) == 0) return true;
    }
    return false;
}

/* Queue a payload for the worker; drop-and-count when full (A2). */
static void s_push_rx(const uint8_t addr[6], const uint8_t *data,
                      uint16_t len, bool from_poll)
{
    conn_rx_t rx = { .ts_ms = (uint32_t)(esp_timer_get_time() / 1000),
                     .len = len, .from_poll = from_poll };
    memcpy(rx.addr, addr, 6);
    memcpy(rx.data, data, len);
    if (xQueueSend(s_rx_q, &rx, 0) != pdTRUE) {
        xSemaphoreTake(s_mux, portMAX_DELAY);
        s_status.dropped++;
        xSemaphoreGive(s_mux);
    } else if (from_poll) {
        xSemaphoreTake(s_mux, portMAX_DELAY);
        s_status.rx_read++;
        xSemaphoreGive(s_mux);
    } else {
        xSemaphoreTake(s_mux, portMAX_DELAY);
        s_status.rx_notify++;
        xSemaphoreGive(s_mux);
    }
}

/* ---- Completion / failure paths ----------------------------------------
 * Each takes s_mux for its state mutations, releases it before calling
 * into NimBLE, the event callback, or FreeRTOS queues.
 */

static void s_resume_scan_if_paused(void)
{
    /* caller holds s_mux */
    if (s_scan_paused) {
        s_scan_paused = false;
        ble_scan_resume();
    }
}

static void s_post_disc_notice(const uint8_t addr[6])
{
    conn_op_t op = { .type = CONN_OP_DISC_NOTICE };
    if (addr != NULL) memcpy(op.addr, addr, 6);
    xQueueSend(s_op_q, &op, 0);
}

/* Link came fully up (subscribed or polling). */
static void s_finish_active(bool subscribed)
{
    uint8_t addr[6];

    xSemaphoreTake(s_mux, portMAX_DELAY);
    memcpy(addr, s_status.peer_addr, 6);
    s_state = BLE_CONN_STATE_ACTIVE;
    s_status.state = BLE_CONN_STATE_ACTIVE;
    s_status.subscribed = subscribed;
    s_status.polling = !subscribed;
    s_next_poll_ms = (uint32_t)(esp_timer_get_time() / 1000) +
                     s_status.poll_interval_ms;
    s_resume_scan_if_paused();
    bool waited = s_direct_wait;
    s_direct_wait = false;
    xSemaphoreGive(s_mux);

    if (waited) xSemaphoreGive(s_start_sem);
    if (s_event_cb != NULL) s_event_cb(true, addr);
}

/* Discovery/CCCD failure with a live connection: terminate; the DISCONNECT
 * event performs the final cleanup and resolves any direct-start wait. */
static void s_fail_connected(int code)
{
    xSemaphoreTake(s_mux, portMAX_DELAY);
    s_status.errors++;
    s_fail_code = code;
    uint16_t handle = s_conn_handle;
    xSemaphoreGive(s_mux);
    ble_gap_terminate(handle, BLE_ERR_REM_USER_CONN_TERM);
}

/* Failure with no connection (connect attempt failed or was cancelled). */
static void s_fail_no_conn(int code)
{
    uint8_t addr[6];

    xSemaphoreTake(s_mux, portMAX_DELAY);
    memcpy(addr, s_status.peer_addr, 6);
    s_state = BLE_CONN_STATE_OFF;
    s_status.state = BLE_CONN_STATE_OFF;
    s_conn_handle = 0xFFFF;
    bool was_link = (s_status.connects > 0);
    s_resume_scan_if_paused();
    bool waited = s_direct_wait;
    s_direct_wait = false;
    if (waited) s_fail_code = code;
    xSemaphoreGive(s_mux);

    if (waited) xSemaphoreGive(s_start_sem);
    if (was_link && s_event_cb != NULL) s_event_cb(false, addr);
}

/* ---- GATT procedure callbacks (host task) ------------------------------ */

static int s_svc_disc_cb(uint16_t conn, const struct ble_gatt_error *error,
                         const struct ble_gatt_svc *svc, void *arg)
{
    (void)conn; (void)arg;
    if (error != NULL && error->status == BLE_HS_EDONE) {
        if (s_svc_found) {
            return ble_gattc_disc_all_chrs(s_conn_handle, s_svc_start,
                                           s_svc_end, s_chr_disc_cb, NULL);
        }
        s_fail_connected(-454);
        return 0;
    }
    if (error != NULL || svc == NULL) {
        s_fail_connected(-454);
        return 0;
    }
    s_svc_start = svc->start_handle;
    s_svc_end = svc->end_handle;
    s_svc_found = true;
    return 0;
}

static int s_chr_disc_cb(uint16_t conn, const struct ble_gatt_error *error,
                         const struct ble_gatt_chr *chr, void *arg)
{
    (void)conn; (void)arg;
    if (error != NULL && error->status == BLE_HS_EDONE) {
        if (!s_chr_found) {
            s_fail_connected(-454);
            return 0;
        }
        if (s_chr_props & (BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_INDICATE)) {
            return ble_gattc_disc_all_dscs(s_conn_handle,
                                           (uint16_t)(s_chr_def + 1), s_svc_end,
                                           s_dsc_disc_cb, NULL);
        }
        if (s_chr_props & BLE_GATT_CHR_F_READ) {
            s_finish_active(false);        /* poll fallback */
            return 0;
        }
        s_fail_connected(-454);
        return 0;
    }
    if (error != NULL || chr == NULL) {
        s_fail_connected(-454);
        return 0;
    }

    /* Candidate selection: explicit target UUID wins; otherwise auto-pick
     * notify > indicate > readable (proposal §2.3). */
    bool want;
    if (s_have_chr) {
        want = (ble_uuid_cmp(&chr->uuid.u, &s_chr_uuid.u) == 0);
    } else if (chr->properties & BLE_GATT_CHR_F_NOTIFY) {
        want = !s_chr_found;               /* first notify wins */
    } else if (chr->properties & BLE_GATT_CHR_F_INDICATE) {
        want = !s_chr_found;
    } else if (chr->properties & BLE_GATT_CHR_F_READ) {
        want = !s_chr_found;
    } else {
        want = false;
    }
    if (want) {
        s_chr_def = chr->def_handle;
        s_chr_val = chr->val_handle;
        s_chr_props = chr->properties;
        s_chr_found = true;
    }
    return 0;
}

static int s_dsc_disc_cb(uint16_t conn, const struct ble_gatt_error *error,
                         uint16_t chr_val_handle,
                         const struct ble_gatt_dsc *dsc, void *arg)
{
    (void)conn; (void)chr_val_handle; (void)arg;
    if (error != NULL && error->status == BLE_HS_EDONE) {
        if (s_cccd_handle != 0) {
            uint16_t cccd = (s_chr_props & BLE_GATT_CHR_F_NOTIFY)
                                ? CONN_CCCD_NOTIFY : CONN_CCCD_INDICATE;
            return ble_gattc_write_flat(s_conn_handle, s_cccd_handle,
                                        &cccd, sizeof(cccd),
                                        s_cccd_write_cb, NULL);
        }
        if (s_chr_props & BLE_GATT_CHR_F_READ) {
            s_finish_active(false);        /* no CCCD — poll what we have */
            return 0;
        }
        s_fail_connected(-454);
        return 0;
    }
    if (error != NULL || dsc == NULL) {
        s_fail_connected(-454);
        return 0;
    }
    if (ble_uuid_u16(&dsc->uuid.u) == CONN_CCCD_UUID16) {
        s_cccd_handle = dsc->handle;
    }
    return 0;
}

static int s_cccd_write_cb(uint16_t conn, const struct ble_gatt_error *error,
                           struct ble_gatt_attr *attr, void *arg)
{
    (void)conn; (void)attr; (void)arg;
    if (error != NULL && error->status == 0) {
        s_finish_active(true);
    } else if (s_chr_props & BLE_GATT_CHR_F_READ) {
        s_finish_active(false);            /* subscribe failed — poll */
    } else {
        s_fail_connected(-454);
    }
    return 0;
}

static int s_read_cb(uint16_t conn, const struct ble_gatt_error *error,
                     struct ble_gatt_attr *attr, void *arg)
{
    (void)conn; (void)arg;
    if (error != NULL || attr == NULL) {
        xSemaphoreTake(s_mux, portMAX_DELAY);
        s_status.errors++;
        xSemaphoreGive(s_mux);
        return 0;
    }
    uint16_t len = OS_MBUF_PKTLEN(attr->om);
    if (len > BLE_CONN_PAYLOAD_MAX) len = BLE_CONN_PAYLOAD_MAX;
    uint8_t addr[6];
    xSemaphoreTake(s_mux, portMAX_DELAY);
    memcpy(addr, s_status.peer_addr, 6);
    xSemaphoreGive(s_mux);
    s_push_rx(addr, attr->om->om_data, len, true);
    return 0;
}

/* ---- GAP events (host task) ------------------------------------------- */

static int s_gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;

    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT: {
        if (event->connect.status == 0) {
            xSemaphoreTake(s_mux, portMAX_DELAY);
            s_conn_handle = event->connect.conn_handle;
            s_status.connects++;
            s_status.mtu = 0;
            s_svc_found = s_chr_found = false;
            s_cccd_handle = 0;
            s_chr_props = 0;
            s_state = BLE_CONN_STATE_DISCOVERING;
            s_status.state = BLE_CONN_STATE_DISCOVERING;
            xSemaphoreGive(s_mux);

            ble_gattc_exchange_mtu(s_conn_handle, NULL, NULL);   /* pin MTU (A5) */
            int rc = ble_gattc_disc_svc_by_uuid(s_conn_handle, &s_svc_uuid.u,
                                                s_svc_disc_cb, NULL);
            if (rc != 0) {
                s_fail_connected(-454);
            }
            return 0;
        }

        /* Establish failed: timeout, cancellation (CONN STOP), or error.
         * A CONN STOP already moved the state to OFF — treat as user
         * cancel, not a failure, but still unpause the scan. */
        xSemaphoreTake(s_mux, portMAX_DELAY);
        bool user_stop = (s_state == BLE_CONN_STATE_OFF);
        xSemaphoreGive(s_mux);
        s_fail_no_conn(user_stop ? 0 : -455);
        s_post_disc_notice(s_status.peer_addr);
        return 0;
    }

    case BLE_GAP_EVENT_MTU:
        xSemaphoreTake(s_mux, portMAX_DELAY);
        s_status.mtu = event->mtu.value;   /* ATT channel (our exchange) */
        xSemaphoreGive(s_mux);
        return 0;

    case BLE_GAP_EVENT_DISCONNECT: {
        uint8_t addr[6];
        int reason = event->disconnect.reason;

        xSemaphoreTake(s_mux, portMAX_DELAY);
        ble_conn_state_t prev = s_state;
        memcpy(addr, event->disconnect.conn.peer_id_addr.val, 6);
        if (prev != BLE_CONN_STATE_OFF) {
            s_state = BLE_CONN_STATE_OFF;
            s_status.state = BLE_CONN_STATE_OFF;
            s_status.disconnects++;
            s_conn_handle = 0xFFFF;
            s_resume_scan_if_paused();
        }
        bool waited = s_direct_wait;
        s_direct_wait = false;
        /* s_fail_code stays for the CLI to read after the sem; zero only
         * when nobody is waiting (stale from a previous attempt). */
        if (!waited) s_fail_code = 0;
        xSemaphoreGive(s_mux);

        if (prev == BLE_CONN_STATE_OFF) {
            return 0;                      /* stale event after our own stop */
        }
        if (waited) xSemaphoreGive(s_start_sem);
        if (s_event_cb != NULL) s_event_cb(false, addr);
        s_post_disc_notice(addr);
        printf("CONN: disconnected (reason=%d, was %s)\n",
               reason, s_state_name(prev));
        return 0;
    }

    case BLE_GAP_EVENT_NOTIFY_RX: {
        if (event->notify_rx.conn_handle != s_conn_handle ||
            event->notify_rx.attr_handle != s_chr_val) {
            return 0;
        }
        struct os_mbuf *om = event->notify_rx.om;
        uint16_t len = OS_MBUF_PKTLEN(om);
        if (len > BLE_CONN_PAYLOAD_MAX) len = BLE_CONN_PAYLOAD_MAX;
        uint8_t addr[6];
        xSemaphoreTake(s_mux, portMAX_DELAY);
        memcpy(addr, s_status.peer_addr, 6);
        xSemaphoreGive(s_mux);
        s_push_rx(addr, om->om_data, len, false);
        return 0;
    }

    case BLE_GAP_EVENT_DISC: {
        /* Our own peer-search discovery (only when no user scan runs). */
        struct ble_gap_disc_desc *disc = &event->disc;
        xSemaphoreTake(s_mux, portMAX_DELAY);
        bool match = (s_state == BLE_CONN_STATE_PEER_SEARCH) && s_own_disc &&
                     s_adv_matches_target(disc->data, disc->length_data);
        if (!match) {
            xSemaphoreGive(s_mux);
            return 0;
        }
        conn_op_t op = { .type = CONN_OP_CONNECT };
        memcpy(op.addr, disc->addr.val, 6);
        op.addr_type = disc->addr.type;
        memcpy(s_status.peer_addr, disc->addr.val, 6);
        s_status.peer_addr_type = disc->addr.type;
        s_state = BLE_CONN_STATE_CONNECTING;
        s_status.state = BLE_CONN_STATE_CONNECTING;
        s_own_disc = false;
        xSemaphoreGive(s_mux);

        ble_gap_disc_cancel();
        xQueueSend(s_op_q, &op, 0);
        return 0;
    }

    case BLE_GAP_EVENT_DISC_COMPLETE:
        /* Our peer-search window ended without a match — restart while
         * still searching (only relevant for the own-disc path). */
        xSemaphoreTake(s_mux, portMAX_DELAY);
        bool restart = s_own_disc && s_state == BLE_CONN_STATE_PEER_SEARCH;
        s_own_disc = restart ? s_own_disc : false;
        xSemaphoreGive(s_mux);
        if (restart) {
            struct ble_gap_disc_params p = { .itvl = 0, .window = 0,
                                             .passive = 1,
                                             .filter_duplicates = 0 };
            if (ble_gap_disc(BLE_OWN_ADDR_PUBLIC, BLE_HS_FOREVER, &p,
                             s_gap_event, NULL) != 0) {
                s_own_disc = false;
            }
        }
        return 0;

    default:
        return 0;
    }
}

/* ---- Scan tap (host task, called from ble_scan's DISC handler) -------- */

static void s_tap(const adv_report_raw_t *report)
{
    xSemaphoreTake(s_mux, portMAX_DELAY);

    /* Address-type auto-learn for a pending direct connect (A3). */
    if (s_learning && memcmp(s_learn_addr, report->addr, 6) == 0) {
        s_learned_type = report->addr_type;
    }

    if (s_state != BLE_CONN_STATE_PEER_SEARCH || !s_have_svc) {
        xSemaphoreGive(s_mux);
        return;
    }
    if (!s_adv_matches_target(report->adv_data, report->adv_data_len)) {
        xSemaphoreGive(s_mux);
        return;
    }

    conn_op_t op = { .type = CONN_OP_CONNECT };
    memcpy(op.addr, report->addr, 6);
    op.addr_type = report->addr_type;
    memcpy(s_status.peer_addr, report->addr, 6);
    s_status.peer_addr_type = report->addr_type;
    s_state = BLE_CONN_STATE_CONNECTING;
    s_status.state = BLE_CONN_STATE_CONNECTING;
    xSemaphoreGive(s_mux);
    xQueueSend(s_op_q, &op, 0);
}

/* ---- Worker task ------------------------------------------------------- */

static void s_emit(const conn_rx_t *rx)
{
    char addr_str[18];
    char line[JSON_LINE_MAX_LEN];

    s_format_addr(rx->addr, addr_str, sizeof(addr_str));
    if (json_encode_conn(addr_str, rx->ts_ms, rx->data, rx->len,
                         line, sizeof(line), NULL) != 0) {
        xSemaphoreTake(s_mux, portMAX_DELAY);
        s_status.dropped++;
        xSemaphoreGive(s_mux);
        return;
    }
    bool ok = (usb_console_send_json(line) == 0);
    xSemaphoreTake(s_mux, portMAX_DELAY);
    if (ok) s_status.tx_lines++;
    else s_status.dropped++;
    xSemaphoreGive(s_mux);
}

/* Connect sequence — in the worker so the EBUSY retry loop has somewhere
 * to sleep and the CLI only waits on the semaphore. */
static void s_worker_connect(const uint8_t addr[6], uint8_t addr_type)
{
    if (addr_type == CONN_ADDR_TYPE_SEARCH) {
        /* Auto-connect with no user scan: run our own discovery. The tap
         * covers the scan-active case. If a scan started meanwhile, the
         * discovery returns EBUSY and we quietly fall back to the tap. */
        xSemaphoreTake(s_mux, portMAX_DELAY);
        bool go = (s_state == BLE_CONN_STATE_PEER_SEARCH);
        xSemaphoreGive(s_mux);
        if (!go) return;

        struct ble_gap_disc_params p = { .itvl = 0, .window = 0, .passive = 1,
                                         .filter_duplicates = 0 };
        int rc = ble_gap_disc(BLE_OWN_ADDR_PUBLIC, BLE_HS_FOREVER, &p,
                              s_gap_event, NULL);
        xSemaphoreTake(s_mux, portMAX_DELAY);
        s_own_disc = (rc == 0);
        xSemaphoreGive(s_mux);
        if (rc != 0) {
            printf("CONN: own discovery not started (%d) — tap path only\n", rc);
        }
        return;
    }

    xSemaphoreTake(s_mux, portMAX_DELAY);
    if (s_state != BLE_CONN_STATE_CONNECTING) {   /* a stop raced us */
        xSemaphoreGive(s_mux);
        return;
    }
    xSemaphoreGive(s_mux);

    /* One GAP procedure at a time: pause the user scan around the connect
     * (the connect/disconnect finalizers resume it). */
    if (ble_scan_is_active()) {
        ble_scan_pause();
        xSemaphoreTake(s_mux, portMAX_DELAY);
        s_scan_paused = true;
        xSemaphoreGive(s_mux);
    }

    ble_addr_t peer;
    memcpy(peer.val, addr, 6);
    peer.type = addr_type;

    int rc = BLE_HS_EBUSY;
    for (int attempt = 0; attempt < CONN_CONNECT_RETRIES; attempt++) {
        rc = ble_gap_connect(BLE_OWN_ADDR_PUBLIC, &peer,
                             CONN_CONNECT_TIMEOUT_MS, NULL, s_gap_event, NULL);
        if (rc != BLE_HS_EBUSY) break;
        vTaskDelay(pdMS_TO_TICKS(CONN_CONNECT_RETRY_DELAY_MS));
    }

    if (rc != 0) {
        printf("CONN: connect not started (%d)\n", rc);
        s_fail_no_conn(-455);
        s_post_disc_notice(addr);
    }
    /* rc == 0: the CONNECT event continues the sequence. */
}

static void s_worker_stop(void)
{
    xSemaphoreTake(s_mux, portMAX_DELAY);
    ble_conn_state_t st = s_state;
    uint16_t handle = s_conn_handle;
    bool own = s_own_disc;
    s_own_disc = false;
    if (st == BLE_CONN_STATE_PEER_SEARCH) {
        s_state = BLE_CONN_STATE_OFF;
        s_status.state = BLE_CONN_STATE_OFF;
    }
    xSemaphoreGive(s_mux);

    if (st == BLE_CONN_STATE_PEER_SEARCH && own) {
        ble_gap_disc_cancel();
        return;
    }
    if (st == BLE_CONN_STATE_CONNECTING) {
        ble_gap_conn_cancel();                   /* CONNECT-fail event finalizes */
    } else if (st == BLE_CONN_STATE_DISCOVERING ||
               st == BLE_CONN_STATE_ACTIVE) {
        ble_gap_terminate(handle, BLE_ERR_REM_USER_CONN_TERM);
    }
}

static void s_worker_task(void *param)
{
    (void)param;
    conn_op_t op;
    conn_rx_t rx;

    for (;;) {
        if (xQueueReceive(s_op_q, &op, pdMS_TO_TICKS(100)) == pdTRUE) {
            switch (op.type) {
            case CONN_OP_CONNECT:
                s_worker_connect(op.addr, op.addr_type);
                break;
            case CONN_OP_STOP:
                s_worker_stop();
                break;
            case CONN_OP_DISC_NOTICE: {
                char addr_str[18];
                char line[80];
                s_format_addr(op.addr, addr_str, sizeof(addr_str));
                snprintf(line, sizeof(line),
                         "{\"status\":\"ok\",\"cmd\":\"conn_disconnected\","
                         "\"addr\":\"%s\"}", addr_str);
                usb_console_send_json(line);
                break;
            }
            default:
                break;
            }
            continue;
        }

        /* Poll fallback: only when active without a subscription. */
        uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
        bool do_poll = false;
        xSemaphoreTake(s_mux, portMAX_DELAY);
        if (s_state == BLE_CONN_STATE_ACTIVE && s_status.polling &&
            (int32_t)(now_ms - s_next_poll_ms) >= 0) {
            s_next_poll_ms = now_ms + s_status.poll_interval_ms;
            do_poll = true;
        }
        uint16_t conn_handle = s_conn_handle;
        uint16_t chr_val = s_chr_val;
        xSemaphoreGive(s_mux);
        if (do_poll && ble_gattc_read(conn_handle, chr_val,
                                      s_read_cb, NULL) != 0) {
            xSemaphoreTake(s_mux, portMAX_DELAY);
            s_status.errors++;
            xSemaphoreGive(s_mux);
        }

        while (xQueueReceive(s_rx_q, &rx, 0) == pdTRUE) {
            s_emit(&rx);
        }
    }
}

/* ---- Public API (CLI task) -------------------------------------------- */

int ble_conn_init(void)
{
    if (s_worker != NULL) {
        return 0;                           /* idempotent */
    }

    s_mux = xSemaphoreCreateMutex();
    s_op_q = xQueueCreate(CONN_OP_QUEUE_DEPTH, sizeof(conn_op_t));
    s_rx_q = xQueueCreate(BLE_CONN_QUEUE_DEPTH, sizeof(conn_rx_t));
    s_start_sem = xSemaphoreCreateBinary();
    if (s_mux == NULL || s_op_q == NULL || s_rx_q == NULL ||
        s_start_sem == NULL) {
        return -1;
    }

    memset(&s_status, 0, sizeof(s_status));
    s_status.poll_interval_ms = BLE_CONN_POLL_DEFAULT_MS;
    s_state = BLE_CONN_STATE_OFF;

    if (xTaskCreatePinnedToCore(s_worker_task, "conn_worker",
                                CONN_WORKER_STACK, NULL, CONN_WORKER_PRIO,
                                &s_worker, 0) != pdPASS) {
        s_worker = NULL;
        return -1;
    }

    ble_scan_set_tap(s_tap);
    printf("CONN: feature enabled (central role, 1 connection)\n");
    return 0;
}

void ble_conn_set_event_cb(ble_conn_event_fn cb)
{
    s_event_cb = cb;
}

int ble_conn_set_target(const char *svc_uuid, const char *chr_uuid)
{
    if (svc_uuid == NULL) return -450;

    ble_uuid_any_t svc, chr;
    bool have_chr = false;
    if (s_parse_uuid(svc_uuid, &svc) != 0) return -450;
    if (chr_uuid != NULL) {
        if (s_parse_uuid(chr_uuid, &chr) != 0) return -450;
        have_chr = true;
    }

    xSemaphoreTake(s_mux, portMAX_DELAY);
    if (s_state != BLE_CONN_STATE_OFF) {
        xSemaphoreGive(s_mux);
        return -452;
    }
    s_svc_uuid = svc;
    s_chr_uuid = chr;
    s_have_svc = true;
    s_have_chr = have_chr;
    xSemaphoreGive(s_mux);
    return 0;
}

int ble_conn_start(const char *addr_str, const char *addr_type)
{
    conn_op_t op = { .type = CONN_OP_CONNECT };

    xSemaphoreTake(s_mux, portMAX_DELAY);
    if (s_state != BLE_CONN_STATE_OFF) {
        xSemaphoreGive(s_mux);
        return -452;
    }
    if (addr_str == NULL) {
        if (!s_have_svc) {
            xSemaphoreGive(s_mux);
            return -456;
        }
        op.addr_type = CONN_ADDR_TYPE_SEARCH;
        s_state = BLE_CONN_STATE_PEER_SEARCH;
        s_status.state = BLE_CONN_STATE_PEER_SEARCH;
        xSemaphoreGive(s_mux);
        xQueueSend(s_op_q, &op, 0);
        return 0;                           /* async — watch CONN STATUS */
    }

    if (s_parse_addr(addr_str, op.addr) != 0) {
        xSemaphoreGive(s_mux);
        return -450;
    }

    /* Address type: explicit argument > tap auto-learn > public default
     * (review A3 — random-static peers fail silently with the wrong type). */
    uint8_t type;
    if (addr_type != NULL && strcasecmp(addr_type, "random") == 0) {
        type = BLE_ADDR_RANDOM;
    } else if (addr_type != NULL && strcasecmp(addr_type, "public") == 0) {
        type = BLE_ADDR_PUBLIC;
    } else if (s_learning && memcmp(s_learn_addr, op.addr, 6) == 0 &&
               s_learned_type != 0xFF) {
        type = s_learned_type;
    } else {
        type = BLE_ADDR_PUBLIC;
    }
    op.addr_type = type;
    s_learning = false;

    s_state = BLE_CONN_STATE_CONNECTING;
    s_status.state = BLE_CONN_STATE_CONNECTING;
    memcpy(s_status.peer_addr, op.addr, 6);
    s_status.peer_addr_type = type;
    s_direct_wait = true;
    s_fail_code = 0;
    xSemaphoreGive(s_mux);

    while (xSemaphoreTake(s_start_sem, 0) == pdTRUE) {}   /* drain stale */
    xQueueSend(s_op_q, &op, 0);

    /* Block until up or failed — bounded by connect timeout + retries. */
    const uint32_t wait_ms = CONN_CONNECT_TIMEOUT_MS +
        CONN_CONNECT_RETRIES * CONN_CONNECT_RETRY_DELAY_MS + 1500;
    if (xSemaphoreTake(s_start_sem, pdMS_TO_TICKS(wait_ms)) != pdTRUE) {
        xSemaphoreTake(s_mux, portMAX_DELAY);
        bool waited = s_direct_wait;
        s_direct_wait = false;
        xSemaphoreGive(s_mux);
        return waited ? -455 : 0;
    }

    xSemaphoreTake(s_mux, portMAX_DELAY);
    int code = s_fail_code;
    s_fail_code = 0;
    xSemaphoreGive(s_mux);
    return code;
}

int ble_conn_stop(void)
{
    xSemaphoreTake(s_mux, portMAX_DELAY);
    ble_conn_state_t st = s_state;
    uint16_t handle = s_conn_handle;
    bool own = s_own_disc;
    s_own_disc = false;
    s_learning = false;

    if (st == BLE_CONN_STATE_OFF) {
        xSemaphoreGive(s_mux);
        return -453;
    }
    if (st == BLE_CONN_STATE_PEER_SEARCH) {
        s_state = BLE_CONN_STATE_OFF;
        s_status.state = BLE_CONN_STATE_OFF;
    }
    /* CONNECTING/DISCOVERING/ACTIVE finalize via the DISCONNECT event. */
    xSemaphoreGive(s_mux);

    if (st == BLE_CONN_STATE_PEER_SEARCH && own) {
        ble_gap_disc_cancel();
        return 0;
    }
    if (st == BLE_CONN_STATE_CONNECTING) {
        ble_gap_conn_cancel();
    } else {
        ble_gap_terminate(handle, BLE_ERR_REM_USER_CONN_TERM);
    }
    return 0;
}

/* Lock-free enum reads are atomic on this target. */
ble_conn_state_t ble_conn_get_state(void)
{
    return s_state;
}

bool ble_conn_is_active(void)
{
    return s_state == BLE_CONN_STATE_ACTIVE;
}

void ble_conn_get_status(ble_conn_status_t *out)
{
    if (out == NULL) return;
    xSemaphoreTake(s_mux, portMAX_DELAY);
    memcpy(out, &s_status, sizeof(*out));
    out->state = s_state;
    xSemaphoreGive(s_mux);
}

int ble_conn_set_poll_interval(uint32_t ms)
{
    if (ms < BLE_CONN_POLL_MIN_MS || ms > BLE_CONN_POLL_MAX_MS) {
        return -450;
    }
    xSemaphoreTake(s_mux, portMAX_DELAY);
    s_status.poll_interval_ms = ms;
    xSemaphoreGive(s_mux);
    return 0;
}
