/*
 * Shared subsystem stubs for host tests — see test_stubs.h.
 */

#include <string.h>
#include <stdio.h>
#include "test_stubs.h"
#include "lua_if.h"
#include "script_if.h"
#include "storage_if.h"
#include "power_if.h"

bool     stub_ble_ready;
bool     stub_scanning;
bool     stub_script_loaded;
bool     stub_script_running;
uint32_t stub_interval_ms;
int      stub_scan_start_ret;
int      stub_pipeline_start_ret;
int      stub_script_run_ret;

int      stub_upload_end_ret;
uint32_t stub_upload_chunk_calls;
uint32_t stub_uploaded_bytes;
bool     stub_upload_active;
char     stub_upload_end_err[128];

int      stub_lua_exec_ret;
char     stub_lua_exec_result[256];

bool     stub_power_sleep_enabled;

/* ---- BLE connection stubs (F2.4) ---- */

bool     stub_conn_enabled = true;
int      stub_conn_state = BLE_CONN_STATE_OFF;
int      stub_conn_set_target_ret;
int      stub_conn_start_ret;
int      stub_conn_stop_ret;
char     stub_conn_target_svc[40];
char     stub_conn_target_chr[40];
bool     stub_conn_target_chr_set;
char     stub_conn_start_addr[24];
char     stub_conn_start_type[12];
uint32_t stub_conn_poll_ms = 1000;
uint32_t stub_conn_tx_lines;
uint32_t stub_conn_dropped;
uint8_t  stub_conn_peer[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
bool     stub_conn_power_hold;

void stub_reset_all(void)
{
    stub_ble_ready = true;
    stub_scanning = false;
    stub_script_loaded = false;
    stub_script_running = false;
    stub_interval_ms = 0;
    stub_scan_start_ret = 0;
    stub_pipeline_start_ret = 0;
    stub_script_run_ret = 0;
    stub_upload_end_ret = 0;
    stub_upload_chunk_calls = 0;
    stub_uploaded_bytes = 0;
    stub_upload_active = false;
    stub_upload_end_err[0] = '\0';
    stub_lua_exec_ret = 0;
    snprintf(stub_lua_exec_result, sizeof(stub_lua_exec_result), "ok");
    stub_power_sleep_enabled = true;

    stub_conn_enabled = true;
    stub_conn_state = BLE_CONN_STATE_OFF;
    stub_conn_set_target_ret = 0;
    stub_conn_start_ret = 0;
    stub_conn_stop_ret = 0;
    stub_conn_target_svc[0] = '\0';
    stub_conn_target_chr[0] = '\0';
    stub_conn_target_chr_set = false;
    stub_conn_start_addr[0] = '\0';
    stub_conn_start_type[0] = '\0';
    stub_conn_poll_ms = 1000;
    stub_conn_tx_lines = 5;
    stub_conn_dropped = 0;
    stub_conn_power_hold = false;
}

/* ---- BLE ---- */

bool ble_is_ready(void)          { return stub_ble_ready; }
int  ble_scan_start(void)        { if (stub_scan_start_ret == 0) stub_scanning = true; return stub_scan_start_ret; }
int  ble_scan_stop(void)         { stub_scanning = false; return 0; }
bool ble_scan_is_active(void)    { return stub_scanning; }
int  ble_scan_set_params(uint32_t interval_ms, uint32_t window_ms)
{
    (void)window_ms;
    stub_interval_ms = interval_ms;
    return 0;
}
uint32_t ble_scan_get_drop_count(void) { return 0; }
void ble_scan_set_tap(void (*tap)(const adv_report_raw_t *report)) { (void)tap; }
void ble_scan_pause(void) {}
void ble_scan_resume(void) {}

/* ---- BLE connection (F2.4) ---- */

int  ble_conn_init(void)                { return 0; }
void ble_conn_set_event_cb(ble_conn_event_fn cb) { (void)cb; }
int  ble_conn_set_target(const char *svc, const char *chr)
{
    if (stub_conn_set_target_ret != 0) return stub_conn_set_target_ret;
    snprintf(stub_conn_target_svc, sizeof(stub_conn_target_svc), "%s", svc);
    if (chr != NULL) {
        snprintf(stub_conn_target_chr, sizeof(stub_conn_target_chr), "%s", chr);
        stub_conn_target_chr_set = true;
    } else {
        stub_conn_target_chr_set = false;
    }
    return 0;
}
int  ble_conn_start(const char *addr, const char *type)
{
    if (stub_conn_start_ret != 0) return stub_conn_start_ret;
    snprintf(stub_conn_start_addr, sizeof(stub_conn_start_addr), "%s",
             addr ? addr : "");
    snprintf(stub_conn_start_type, sizeof(stub_conn_start_type), "%s",
             type ? type : "");
    stub_conn_state = addr ? BLE_CONN_STATE_ACTIVE : BLE_CONN_STATE_PEER_SEARCH;
    return 0;
}
int  ble_conn_stop(void)
{
    if (stub_conn_stop_ret != 0) return stub_conn_stop_ret;
    stub_conn_state = BLE_CONN_STATE_OFF;
    return 0;
}
ble_conn_state_t ble_conn_get_state(void) { return (ble_conn_state_t)stub_conn_state; }
const char *ble_conn_state_name(ble_conn_state_t st)
{
    switch (st) {
        case BLE_CONN_STATE_PEER_SEARCH: return "peer_search";
        case BLE_CONN_STATE_CONNECTING:  return "connecting";
        case BLE_CONN_STATE_DISCOVERING: return "discovering";
        case BLE_CONN_STATE_ACTIVE:      return "active";
        default:                         return "off";
    }
}
bool ble_conn_is_active(void) { return stub_conn_state == BLE_CONN_STATE_ACTIVE; }
void ble_conn_get_status(ble_conn_status_t *out)
{
    if (out == NULL) return;
    memset(out, 0, sizeof(*out));
    out->state = (ble_conn_state_t)stub_conn_state;
    out->poll_interval_ms = stub_conn_poll_ms;
    out->tx_lines = stub_conn_tx_lines;
    out->dropped = stub_conn_dropped;
    out->subscribed = (stub_conn_state == BLE_CONN_STATE_ACTIVE);
    memcpy(out->peer_addr, stub_conn_peer, 6);
}
int  ble_conn_set_poll_interval(uint32_t ms)
{
    if (ms < BLE_CONN_POLL_MIN_MS || ms > BLE_CONN_POLL_MAX_MS) return -450;
    stub_conn_poll_ms = ms;
    return 0;
}

/* ---- Pipeline ---- */

int  pipeline_init(void)         { return 0; }
int  pipeline_start(void)        { return stub_pipeline_start_ret; }
int  pipeline_stop(void)         { return 0; }
void pipeline_set_filter(filter_engine_t *eng) { (void)eng; }
int  pipeline_get_stats(pipeline_stats_t *stats)
{
    memset(stats, 0, sizeof(*stats));
    return 0;
}

/* ---- Lua engine ---- */

void lua_engine_lock(void)       {}
void lua_engine_unlock(void)     {}
bool lua_engine_is_ready(void)   { return true; }
void lua_engine_pool_stats(uint32_t *used, uint32_t *peak)
{
    if (used != NULL) *used = 1111;
    if (peak != NULL) *peak = 2222;
}
int  lua_engine_init(void)       { return 0; }
int  lua_engine_deinit(void)     { return 0; }
int  lua_engine_exec(const char *script, char *result, uint16_t result_len)
{
    (void)script;
    snprintf(result, result_len, "%s", stub_lua_exec_result);
    return stub_lua_exec_ret;
}

/* ---- Script management ---- */

int  script_upload_begin(void)
{
    if (stub_upload_active) return -611;
    stub_upload_active = true;
    stub_uploaded_bytes = 0;
    stub_upload_chunk_calls = 0;
    return 0;
}

int  script_upload_chunk(const uint8_t *data, uint16_t len)
{
    (void)data;
    if (!stub_upload_active) return -611;
    stub_upload_chunk_calls++;
    stub_uploaded_bytes += len;
    return 0;
}

int  script_upload_end(char *err_buf, uint16_t err_len)
{
    if (!stub_upload_active) return -611;
    stub_upload_active = false;
    if (stub_upload_end_ret == 0) {
        stub_script_loaded = true;
    } else if (err_buf != NULL && err_len > 0) {
        if (stub_upload_end_err[0] != '\0') {
            snprintf(err_buf, err_len, "%s", stub_upload_end_err);
        } else {
            snprintf(err_buf, err_len, "stub compile error");
        }
    }
    return stub_upload_end_ret;
}

int  script_upload_abort(void)
{
    stub_upload_active = false;
    stub_uploaded_bytes = 0;
    return 0;
}

int  script_run(void)
{
    if (!stub_script_loaded) return -611;
    if (stub_script_run_ret == 0) stub_script_running = true;
    return stub_script_run_ret;
}

int  script_stop(void)
{
    stub_script_running = false;
    return 0;
}
bool script_is_loaded(void)      { return stub_script_loaded; }
bool script_is_running(void)     { return stub_script_running; }

/* ---- Storage ---- */

int  storage_get_free_space(uint32_t *free_bytes)
{
    if (free_bytes) *free_bytes = 12345;
    return 0;
}

/* ---- Power ---- */

int  power_init(void)            { return 0; }
int  power_enable_sleep(bool en) { stub_power_sleep_enabled = en; return 0; }
void power_hold_activity(bool hold) { (void)hold; }
void power_hold_conn(bool hold) { stub_conn_power_hold = hold; }

power_state_t power_get_state(void)
{
    if (stub_scanning || stub_script_running) return POWER_STATE_ACTIVE;
    return stub_power_sleep_enabled ? POWER_STATE_LIGHT_SLEEP
                                    : POWER_STATE_ACTIVE;
}

int  power_get_current_ma(uint32_t *ma)
{
    if (!ma) return -502;
    if (stub_scanning) *ma = 45;
    else *ma = stub_power_sleep_enabled ? 8 : 30;
    return 0;
}

int  power_get_config(power_config_t *cfg)
{
    if (!cfg) return -502;
    cfg->sleep_enabled = stub_power_sleep_enabled;
    cfg->idle_timeout_ms = 1000;
    cfg->usb_suspend_wake = false;
    return 0;
}