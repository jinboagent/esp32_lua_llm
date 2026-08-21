/*
 * BLE Connection (F2.4) — ble_conn.c
 *
 * Central-role connection to one peer: auto-connect by advertised service
 * UUID (scan tap or own discovery) or direct connect by address, GATT
 * service/characteristic discovery, subscribe to notifications/indications
 * with periodic-read fallback, and re-stream payloads as "src":"conn" JSON
 * lines on the same USB stream as adv lines.
 *
 * Concurrency (review A6): one module-owned mutex; state is touched from
 * the NimBLE host task (GAP/GATT callbacks) and the CLI task; nothing else
 * takes the lock.
 *
 * Emit path (review A2): GATT callbacks ONLY enqueue into a static queue
 * (depth 8, drop-newest + counter); a dedicated task drains it and writes
 * to USB, so the NimBLE host task never blocks on the USB TX mutex.
 *
 * Zero heap: all buffers static.
 */

#include "ble_if.h"
#include "json_if.h"
#include "usb_if.h"

#include <stdio.h>
#include <string.h>
#include <stdatomic.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "esp_timer.h"

#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"   /* gattc API lives here in NimBLE (IDF v5.1) */
#include "host/ble_hs_adv.h"
#include "host/ble_uuid.h"
#include "os/os_mbuf.h"

#define CONN_QUEUE_DEPTH      8
#define CONN_TASK_STACK       4096
#define CONN_TASK_PRIO        8
#define CONN_POLL_DEFAULT_MS  1000
#define CONN_CONNECT_MS       5000
#define CONN_ADDR_CACHE       8
#define CONN_TERM_REASON      0x16  /* HCI: connection terminated by local host */

#define EVT_BIT_PEER_FOUND    BIT0

typedef struct {
    uint8_t  kind;   /* 0 = data payload, 1 = ready-made notice JSON */
    uint32_t ts_ms;
    uint8_t  addr[6];
    uint16_t len;
    uint8_t  buf[BLE_CONN_PAYLOAD_MAX_LEN];
} conn_item_t;

typedef struct {
    uint8_t addr[6];
    uint8_t addr_type;
    bool    valid;
} addr_cache_t;

static SemaphoreHandle_t  s_mtx = NULL;
static QueueHandle_t      s_q = NULL;
static EventGroupHandle_t s_ev = NULL;

static atomic_int  s_state = BLE_CONN_STATE_IDLE;
static atomic_uint s_rx = 0;
static atomic_uint s_drops = 0;

static ble_uuid_any_t s_svc_uuid;  /* 16/32/128-bit target service UUID */
static ble_uuid_any_t s_char_uuid;
static bool s_has_svc = false;
static bool s_has_char = false;

static uint16_t s_conn_handle = 0;
static uint16_t s_svc_start = 0;
static uint16_t s_svc_end = 0;
static uint16_t s_val_handle = 0;
static uint8_t  s_chr_props = 0;
static uint16_t s_cccd_handle = 0;
static uint8_t  s_sub_flags = 0;   /* 1 = notify, 2 = indicate */
static bool     s_notify_mode = false;
static uint32_t s_poll_ms = CONN_POLL_DEFAULT_MS;
static int64_t  s_next_poll_us = 0;

static uint8_t s_peer_addr[6] = {0};
static uint8_t s_peer_type = 0;
static bool    s_own_disc = false;   /* peer search runs its own discovery */

static addr_cache_t s_cache[CONN_ADDR_CACHE];
static ble_conn_event_cb_t s_event_cb = NULL;

static uint32_t s_now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void s_lock(void)   { if (s_mtx) xSemaphoreTake(s_mtx, portMAX_DELAY); }
static void s_unlock(void) { if (s_mtx) xSemaphoreGive(s_mtx); }

static void s_set_state(int st)
{
    atomic_store(&s_state, st);
}

static void s_fire(int evt)
{
    if (s_event_cb != NULL) {
        s_event_cb(evt);
    }
}

/* ---- emit queue (A2) ---------------------------------------------------- */

static void s_enqueue(uint8_t kind, const uint8_t *payload, uint16_t len)
{
    conn_item_t it;
    memset(&it, 0, sizeof(it));
    it.kind = kind;
    it.ts_ms = s_now_ms();
    memcpy(it.addr, s_peer_addr, 6);
    if (len > BLE_CONN_PAYLOAD_MAX_LEN - 1) {
        len = BLE_CONN_PAYLOAD_MAX_LEN - 1;
    }
    memcpy(it.buf, payload, len);
    it.buf[len] = '\0';
    it.len = len;

    if (xQueueSend(s_q, &it, 0) != pdTRUE) {
        if (kind == 0) {
            atomic_fetch_add(&s_drops, 1);  /* drop-newest */
        } else {
            conn_item_t old;
            xQueueReceive(s_q, &old, 0);    /* notices evict oldest */
            xQueueSend(s_q, &it, 0);
        }
    } else if (kind == 0) {
        atomic_fetch_add(&s_rx, 1);
    }
}

static void s_note(const char *json)
{
    s_enqueue(1, (const uint8_t *)json, (uint16_t)strlen(json));
}

static void s_emit_item(const conn_item_t *it)
{
    if (it->kind == 1) {
        usb_console_send_json((const char *)it->buf);
        return;
    }
    char addr_str[18];
    snprintf(addr_str, sizeof(addr_str), "%02X:%02X:%02X:%02X:%02X:%02X",
             it->addr[0], it->addr[1], it->addr[2],
             it->addr[3], it->addr[4], it->addr[5]);
    char line[JSON_LINE_MAX_LEN];
    uint16_t out_len = 0;
    if (json_encode_conn(it->ts_ms, addr_str, (const char *)it->buf,
                         line, sizeof(line), &out_len) == 0) {
        usb_console_send_json(line);
    }
}

/* ---- address-type cache (A3) -------------------------------------------- */

static void s_cache_update(const uint8_t addr[6], uint8_t type)
{
    for (int i = 0; i < CONN_ADDR_CACHE; i++) {
        if (!s_cache[i].valid || memcmp(s_cache[i].addr, addr, 6) == 0) {
            memcpy(s_cache[i].addr, addr, 6);
            s_cache[i].addr_type = type;
            s_cache[i].valid = true;
            return;
        }
    }
    memcpy(s_cache[0].addr, addr, 6);  /* simple overwrite eviction */
    s_cache[0].addr_type = type;
    s_cache[0].valid = true;
}

static int s_cache_lookup(const uint8_t addr[6])
{
    for (int i = 0; i < CONN_ADDR_CACHE; i++) {
        if (s_cache[i].valid && memcmp(s_cache[i].addr, addr, 6) == 0) {
            return s_cache[i].addr_type;
        }
    }
    return -1;
}

/* Parse "180F" / "0000180F" / 36-char 128-bit forms (this NimBLE build has
 * no ble_uuid_init_from_str). 128-bit canonical strings are big-endian;
 * advertised bytes are little-endian, so store reversed to match the
 * adv parser's raw byte order. */
static int s_parse_uuid_str(const char *str, ble_uuid_any_t *out)
{
    size_t n = strlen(str);
    if (n == 4 || n == 8) {
        unsigned v = 0;
        if (sscanf(str, n == 4 ? "%04x" : "%08x", &v) != 1) {
            return -1;
        }
        if (n == 4) {
            out->u.type = BLE_UUID_TYPE_16;
            out->u16.value = (uint16_t)v;
        } else {
            out->u.type = BLE_UUID_TYPE_32;
            out->u32.value = (uint32_t)v;
        }
        return 0;
    }
    if (n == 36) {
        uint8_t be[16];
        int bi = 0, have = 0;
        unsigned byte = 0;
        for (size_t i = 0; i < n; i++) {
            char c = str[i];
            if (c == '-') continue;
            int d;
            if (c >= '0' && c <= '9') d = c - '0';
            else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
            else return -1;
            byte = (byte << 4) | (unsigned)d;
            if (++have == 2) {
                be[bi++] = (uint8_t)byte;
                byte = 0;
                have = 0;
            }
        }
        if (bi != 16) return -1;
        out->u.type = BLE_UUID_TYPE_128;
        for (int i = 0; i < 16; i++) {
            out->u128.value[i] = be[15 - i];
        }
        return 0;
    }
    return -1;
}

/* ---- service-UUID matching on raw adv data (A4) -------------------------- */

static bool s_adv_matches(const adv_report_raw_t *r)
{
    if (!s_has_svc) {
        return false;
    }
    /* Parse the raw AD structures ourselves: the parsed proto report only
     * carries uuid16, but auto-connect must also see 32/128-bit UUIDs. */
    struct ble_hs_adv_fields fields;
    if (ble_hs_adv_parse_fields(&fields, r->adv_data, r->adv_data_len) != 0) {
        return false;
    }
    /* This NimBLE build hands back parsed ble_uuid*_t arrays. */
    for (uint8_t i = 0; i < fields.num_uuids16; i++) {
        if (ble_uuid_cmp(&fields.uuids16[i].u, &s_svc_uuid.u) == 0) return true;
    }
    for (uint8_t i = 0; i < fields.num_uuids32; i++) {
        if (ble_uuid_cmp(&fields.uuids32[i].u, &s_svc_uuid.u) == 0) return true;
    }
    for (uint8_t i = 0; i < fields.num_uuids128; i++) {
        if (ble_uuid_cmp(&fields.uuids128[i].u, &s_svc_uuid.u) == 0) return true;
    }
    return false;
}

/* Raw tap fed by ble_scan for every DISC event (before dedup). */
static void s_tap_cb(const adv_report_raw_t *r)
{
    s_cache_update(r->addr, r->addr_type);
    if (atomic_load(&s_state) == BLE_CONN_STATE_PEER_SEARCH && !s_own_disc) {
        if (s_adv_matches(r)) {
            s_lock();
            memcpy(s_peer_addr, r->addr, 6);
            s_peer_type = r->addr_type;
            s_unlock();
            xEventGroupSetBits(s_ev, EVT_BIT_PEER_FOUND);
        }
    }
}

/* ---- GAP / GATT callbacks (NimBLE host task) ----------------------------- */

static int s_mtu_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                    uint16_t mtu, void *arg)
{
    (void)conn_handle; (void)error; (void)mtu; (void)arg;
    return 0;  /* A5: exchange pinned at init, result not load-bearing */
}

static void s_disc_fail(void)
{
    s_note("{\"status\":\"error\",\"cmd\":\"conn_event\","
           "\"msg\":\"discovery failed\"}");
    ble_gap_terminate(s_conn_handle, CONN_TERM_REASON);
}

static void s_activate(bool notify)
{
    s_lock();
    s_notify_mode = notify;
    s_unlock();
    if (!notify) {
        s_next_poll_us = esp_timer_get_time() + (int64_t)s_poll_ms * 1000;
    }
    s_set_state(BLE_CONN_STATE_ACTIVE);
    printf("BLE conn: active (mode=%s)\n", notify ? "notify" : "poll");
}

/* A5: one notification/read = one line, no reassembly */
static void s_copy_om_to_queue(struct os_mbuf *om)
{
    if (om == NULL) {
        return;
    }
    uint16_t len = OS_MBUF_PKTLEN(om);
    if (len > BLE_CONN_PAYLOAD_MAX_LEN - 1) {
        len = BLE_CONN_PAYLOAD_MAX_LEN - 1;
    }
    uint8_t tmp[BLE_CONN_PAYLOAD_MAX_LEN];
    if (os_mbuf_copydata(om, 0, len, tmp) == 0) {
        s_enqueue(0, tmp, len);
    }
}

static int s_read_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                     struct ble_gatt_attr *attr, void *arg)
{
    (void)conn_handle; (void)arg;
    if (error->status == 0) {
        s_copy_om_to_queue(attr->om);
    }
    return 0;
}

static int s_write_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                      struct ble_gatt_attr *attr, void *arg)
{
    (void)conn_handle; (void)attr; (void)arg;
    if (error->status == 0 || error->status == BLE_HS_EDONE) {
        s_activate(true);
    } else {
        s_activate(false);  /* CCCD write failed -> poll fallback */
    }
    return 0;
}

/* Descriptor discovery: find the CCCD (0x2902) of the chosen
 * characteristic, then write the subscription flags (this NimBLE build
 * has no ble_gattc_subscribe). */
static int s_dsc_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                    uint16_t chr_val_handle, const struct ble_gatt_dsc *dsc,
                    void *arg)
{
    (void)chr_val_handle; (void)arg;
    if (error->status == 0) {
        if (dsc->uuid.u.type == BLE_UUID_TYPE_16 &&
            dsc->uuid.u16.value == BLE_GATT_DSC_CLT_CFG_UUID16) {
            s_cccd_handle = dsc->handle;
        }
        return 0;
    }
    if (error->status == BLE_HS_EDONE) {
        if (s_cccd_handle != 0) {
            uint8_t v = s_sub_flags;
            if (ble_gattc_write_flat(conn_handle, s_cccd_handle, &v,
                                     sizeof(v), s_write_cb, NULL) != 0) {
                s_activate(false);
            }
        } else {
            s_activate(false);  /* no CCCD -> poll fallback */
        }
        return 0;
    }
    s_disc_fail();
    return 0;
}

static int s_chr_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                    const struct ble_gatt_chr *chr, void *arg)
{
    (void)arg;
    if (error->status == 0) {
        bool uuid_ok = !s_has_char ||
                       ble_uuid_cmp(&chr->uuid.u, &s_char_uuid.u) == 0;
        bool usable = chr->properties &
                      (BLE_GATT_CHR_PROP_NOTIFY | BLE_GATT_CHR_PROP_INDICATE |
                       BLE_GATT_CHR_PROP_READ);
        if (uuid_ok && usable && s_val_handle == 0) {
            s_val_handle = chr->val_handle;
            s_chr_props = chr->properties;
        }
        return 0;
    }
    if (error->status == BLE_HS_EDONE) {
        if (s_val_handle == 0) {
            s_disc_fail();
            return 0;
        }
        if (s_chr_props & (BLE_GATT_CHR_PROP_NOTIFY | BLE_GATT_CHR_PROP_INDICATE)) {
            s_cccd_handle = 0;
            s_sub_flags = 0;
            if (s_chr_props & BLE_GATT_CHR_PROP_NOTIFY) s_sub_flags |= 1;
            if (s_chr_props & BLE_GATT_CHR_PROP_INDICATE) s_sub_flags |= 2;
            if (ble_gattc_disc_all_dscs(conn_handle, s_val_handle,
                                        s_svc_end, s_dsc_cb, NULL) != 0) {
                s_disc_fail();
            }
        } else {
            s_activate(false);  /* poll fallback */
        }
        return 0;
    }
    s_disc_fail();
    return 0;
}

static int s_svc_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                    const struct ble_gatt_svc *service, void *arg)
{
    (void)arg;
    if (error->status == 0) {
        s_svc_start = service->start_handle;
        s_svc_end = service->end_handle;
        return 0;
    }
    if (error->status == BLE_HS_EDONE) {
        if (s_svc_start != 0) {
            ble_gattc_disc_all_chrs(conn_handle, s_svc_start, s_svc_end,
                                    s_chr_cb, NULL);
        } else {
            s_disc_fail();
        }
        return 0;
    }
    s_disc_fail();
    return 0;
}

static int s_gap_cb(struct ble_gap_event *event, void *arg);

static void s_connect_to_peer(void)
{
    ble_addr_t peer = { .type = s_peer_type };
    memcpy(peer.val, s_peer_addr, 6);
    s_set_state(BLE_CONN_STATE_CONNECTING);
    int rc = ble_gap_connect(BLE_OWN_ADDR_PUBLIC, &peer, CONN_CONNECT_MS,
                             NULL, s_gap_cb, NULL);
    if (rc != 0) {
        s_set_state(BLE_CONN_STATE_IDLE);
        s_note("{\"status\":\"error\",\"cmd\":\"conn_event\","
               "\"msg\":\"connect failed\"}");
    }
}

static int s_gap_cb(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_DISC: {
        if (atomic_load(&s_state) != BLE_CONN_STATE_PEER_SEARCH ||
            !s_own_disc) {
            break;
        }
        struct ble_gap_disc_desc *disc = &event->disc;
        adv_report_raw_t r;
        memcpy(r.addr, disc->addr.val, 6);
        r.addr_type = disc->addr.type;
        r.rssi = disc->rssi;
        r.ts_ms = s_now_ms();
        uint8_t n = disc->length_data;
        if (n > BLE_ADV_DATA_MAX_LEN) n = BLE_ADV_DATA_MAX_LEN;
        r.adv_data_len = n;
        memcpy(r.adv_data, disc->data, n);
        s_cache_update(r.addr, r.addr_type);
        if (s_adv_matches(&r)) {
            s_lock();
            memcpy(s_peer_addr, r.addr, 6);
            s_peer_type = r.addr_type;
            s_unlock();
            xEventGroupSetBits(s_ev, EVT_BIT_PEER_FOUND);
        }
        break;
    }
    case BLE_GAP_EVENT_DISC_COMPLETE:
        if (atomic_load(&s_state) == BLE_CONN_STATE_PEER_SEARCH && s_own_disc) {
            struct ble_gap_disc_params params = {
                .itvl = 160, .window = 80, .passive = 1,
                .filter_duplicates = 0,
            };
            ble_gap_disc(BLE_OWN_ADDR_PUBLIC, BLE_HS_FOREVER, &params,
                         s_gap_cb, NULL);
        }
        break;
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            s_conn_handle = event->connect.conn_handle;
            s_svc_start = 0;
            s_val_handle = 0;
            s_chr_props = 0;
            s_set_state(BLE_CONN_STATE_DISCOVERING);
            s_fire(BLE_CONN_EVT_CONNECTED);
            ble_gattc_exchange_mtu(s_conn_handle, s_mtu_cb, NULL);  /* A5 */
            ble_gattc_disc_svc_by_uuid(s_conn_handle, &s_svc_uuid.u,
                                       s_svc_cb, NULL);
            printf("BLE conn: connected\n");
        } else {
            s_set_state(BLE_CONN_STATE_IDLE);
            s_note("{\"status\":\"error\",\"cmd\":\"conn_event\","
                   "\"msg\":\"connect timeout\"}");
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT: {
        int st = atomic_load(&s_state);
        if (st == BLE_CONN_STATE_IDLE) {
            break;
        }
        s_set_state(BLE_CONN_STATE_IDLE);
        s_val_handle = 0;
        s_fire(BLE_CONN_EVT_DISCONNECTED);
        char note[96];
        snprintf(note, sizeof(note),
                 "{\"status\":\"ok\",\"cmd\":\"conn_event\","
                 "\"msg\":\"disconnected\",\"reason\":%d}",
                 event->disconnect.reason);
        s_note(note);
        printf("BLE conn: disconnected (reason=%d)\n",
               event->disconnect.reason);
        break;
    }
    case BLE_GAP_EVENT_NOTIFY_RX:
        /* this NimBLE build delivers notifications via GAP, not GATT */
        if (atomic_load(&s_state) == BLE_CONN_STATE_ACTIVE) {
            s_copy_om_to_queue(event->notify_rx.om);
        }
        break;
    default:
        break;
    }
    return 0;
}

/* ---- emitter / peer-search task (A2) ------------------------------------- */

static void s_task_fn(void *arg)
{
    (void)arg;
    conn_item_t it;
    for (;;) {
        if (xQueueReceive(s_q, &it, pdMS_TO_TICKS(50)) == pdTRUE) {
            s_emit_item(&it);
        }
        if (xEventGroupGetBits(s_ev) & EVT_BIT_PEER_FOUND) {
            xEventGroupClearBits(s_ev, EVT_BIT_PEER_FOUND);
            if (atomic_load(&s_state) == BLE_CONN_STATE_PEER_SEARCH) {
                if (s_own_disc) {
                    ble_gap_disc_cancel();
                    s_own_disc = false;
                }
                s_connect_to_peer();
            }
        }
        if (atomic_load(&s_state) == BLE_CONN_STATE_ACTIVE && !s_notify_mode) {
            int64_t now = esp_timer_get_time();
            if (now >= s_next_poll_us) {
                s_next_poll_us = now + (int64_t)s_poll_ms * 1000;
                ble_gattc_read(s_conn_handle, s_val_handle, s_read_cb, NULL);
            }
        }
    }
}

/* ---- public API ----------------------------------------------------------- */

int ble_conn_init(void)
{
    s_mtx = xSemaphoreCreateMutex();
    s_q = xQueueCreate(CONN_QUEUE_DEPTH, sizeof(conn_item_t));
    s_ev = xEventGroupCreate();
    if (s_mtx == NULL || s_q == NULL || s_ev == NULL) {
        return -1;
    }
    ble_att_set_preferred_mtu(256);  /* A5: deliberate, not IDF default */
    ble_scan_set_tap(s_tap_cb);
    xTaskCreate(s_task_fn, "ble_conn", CONN_TASK_STACK, NULL,
                CONN_TASK_PRIO, NULL);
    printf("BLE conn: initialized (F2.4)\n");
    return 0;
}

int ble_conn_set_target(const char *svc_uuid, const char *char_uuid)
{
    if (svc_uuid == NULL) {
        return BLE_CONN_ERR_INVALID_PARAM;
    }
    if (atomic_load(&s_state) != BLE_CONN_STATE_IDLE) {
        return BLE_CONN_ERR_INVALID_STATE;
    }
    ble_uuid_any_t svc;
    if (s_parse_uuid_str(svc_uuid, &svc) != 0) {
        return BLE_CONN_ERR_INVALID_PARAM;
    }
    ble_uuid_any_t chr;
    if (char_uuid != NULL && s_parse_uuid_str(char_uuid, &chr) != 0) {
        return BLE_CONN_ERR_INVALID_PARAM;
    }
    s_lock();
    s_svc_uuid = svc;
    s_has_svc = true;
    if (char_uuid != NULL) {
        s_char_uuid = chr;
        s_has_char = true;
    } else {
        s_has_char = false;
    }
    s_unlock();
    return 0;
}

static bool s_parse_addr(const char *str, uint8_t out[6])
{
    unsigned a[6];
    if (sscanf(str, "%02x:%02x:%02x:%02x:%02x:%02x",
               &a[0], &a[1], &a[2], &a[3], &a[4], &a[5]) != 6) {
        return false;
    }
    for (int i = 0; i < 6; i++) {
        out[i] = (uint8_t)a[i];
    }
    return true;
}

int ble_conn_start(const char *addr, int addr_type)
{
    if (atomic_load(&s_state) != BLE_CONN_STATE_IDLE) {
        return BLE_CONN_ERR_INVALID_STATE;
    }
    if (addr != NULL) {
        uint8_t parsed[6];
        if (!s_parse_addr(addr, parsed)) {
            return BLE_CONN_ERR_INVALID_PARAM;
        }
        int type = addr_type;
        if (type < 0) {  /* A3: auto-learn from the scan tap */
            type = s_cache_lookup(parsed);
            if (type < 0) {
                type = 0;
            }
        }
        if (type != 0 && type != 1) {
            return BLE_CONN_ERR_INVALID_PARAM;
        }
        s_lock();
        memcpy(s_peer_addr, parsed, 6);
        s_peer_type = (uint8_t)type;
        s_unlock();
        s_connect_to_peer();
        return 0;
    }

    if (!s_has_svc) {
        return BLE_CONN_ERR_NO_TARGET;
    }
    s_set_state(BLE_CONN_STATE_PEER_SEARCH);
    if (!ble_scan_is_active()) {
        /* NimBLE runs one discovery at a time: own search only when the
         * user's scan is idle; otherwise the tap feeds the search. */
        struct ble_gap_disc_params params = {
            .itvl = 160, .window = 80, .passive = 1, .filter_duplicates = 0,
        };
        if (ble_gap_disc(BLE_OWN_ADDR_PUBLIC, BLE_HS_FOREVER, &params,
                         s_gap_cb, NULL) != 0) {
            s_set_state(BLE_CONN_STATE_IDLE);
            return BLE_CONN_ERR_INVALID_STATE;
        }
        s_own_disc = true;
    }
    return 0;
}

int ble_conn_stop(void)
{
    int st = atomic_load(&s_state);
    switch (st) {
    case BLE_CONN_STATE_IDLE:
        return BLE_CONN_ERR_NOT_CONNECTED;
    case BLE_CONN_STATE_PEER_SEARCH:
        if (s_own_disc) {
            ble_gap_disc_cancel();
            s_own_disc = false;
        }
        s_set_state(BLE_CONN_STATE_IDLE);
        s_note("{\"status\":\"ok\",\"cmd\":\"conn_stop\"}");
        return 0;
    case BLE_CONN_STATE_CONNECTING:
        ble_gap_conn_cancel();
        s_set_state(BLE_CONN_STATE_IDLE);
        s_note("{\"status\":\"ok\",\"cmd\":\"conn_stop\"}");
        return 0;
    default:
        ble_gap_terminate(s_conn_handle, CONN_TERM_REASON);
        return 0;  /* DISCONNECT event completes the transition */
    }
}

bool ble_conn_is_active(void)
{
    return atomic_load(&s_state) == BLE_CONN_STATE_ACTIVE;
}

int ble_conn_set_poll_interval(uint32_t ms)
{
    if (ms < 100 || ms > 10000) {
        return BLE_CONN_ERR_INVALID_PARAM;
    }
    s_lock();
    s_poll_ms = ms;
    s_unlock();
    return 0;
}

int ble_conn_get_info(ble_conn_info_t *info)
{
    if (info == NULL) {
        return -1;
    }
    s_lock();
    info->state = (ble_conn_state_t)atomic_load(&s_state);
    memcpy(info->addr, s_peer_addr, 6);
    info->addr_type = s_peer_type;
    info->connected = atomic_load(&s_state) == BLE_CONN_STATE_ACTIVE;
    info->notify_mode = s_notify_mode;
    info->poll_ms = s_poll_ms;
    s_unlock();
    info->rx_lines = atomic_load(&s_rx);
    info->drops = atomic_load(&s_drops);
    return 0;
}

int ble_conn_set_event_cb(ble_conn_event_cb_t cb)
{
    s_event_cb = cb;
    return 0;
}
