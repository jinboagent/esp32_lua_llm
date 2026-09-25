#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "ble_if.h"

/*
 * Shared subsystem stubs for host tests (cli, bridge).
 * Emulate just enough of the ble/lua/script/storage contracts for
 * protocol- and state-machine-level testing.
 */

extern bool     stub_ble_ready;
extern bool     stub_scanning;
extern bool     stub_script_loaded;
extern bool     stub_script_running;
extern uint32_t stub_interval_ms;
extern int      stub_scan_start_ret;
extern int      stub_pipeline_start_ret;
extern int      stub_script_run_ret;

/* script upload stub controls */
extern int      stub_upload_end_ret;     /* 0 ok, -612 compile err, -704 storage */
extern uint32_t stub_upload_chunk_calls;
extern uint32_t stub_uploaded_bytes;
extern bool     stub_upload_active;
/* Custom error text script_upload_end writes on failure ("" = default).
 * Lets tests exercise quoted Lua error messages (H1 regression). */
extern char     stub_upload_end_err[128];

/* Lua exec stub controls (H1 regression: quoted results/error text) */
extern int      stub_lua_exec_ret;
extern char     stub_lua_exec_result[256];

/* H6.1 M2: compile-check stub return + last exec'd script capture */
extern int      stub_lua_compile_ret;
extern char     stub_lua_exec_last[512];

/* H6.1 M2: fake LittleFS backed by the storage stubs */
const uint8_t  *stub_fs_get(const char *path, uint32_t *out_len);
int             stub_fs_count(void);

/* power stub controls */
extern bool     stub_power_sleep_enabled;

/* BLE connection stub controls (F2.4 CLI tests) */
extern bool     stub_conn_enabled;      /* feature flag seen by handlers */
extern int      stub_conn_state;        /* ble_conn_state_t as int       */
extern int      stub_conn_set_target_ret;
extern int      stub_conn_start_ret;
extern int      stub_conn_stop_ret;
extern char     stub_conn_target_svc[40];
extern char     stub_conn_target_chr[40];
extern bool     stub_conn_target_chr_set;
extern char     stub_conn_start_addr[24];
extern char     stub_conn_start_type[12];
extern uint32_t stub_conn_poll_ms;
extern uint32_t stub_conn_tx_lines;
extern uint32_t stub_conn_dropped;
extern uint8_t  stub_conn_peer[6];
extern bool     stub_conn_power_hold;   /* last power_hold_conn argument */

/* Reset every control to its default. */
void stub_reset_all(void);
