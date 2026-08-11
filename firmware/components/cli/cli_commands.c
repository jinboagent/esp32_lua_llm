/*
 * CLI Command Interface (F4.1) — cli_commands.c
 *
 * Text command parser/dispatcher with a 3-state machine
 * (IDLE / SCANNING / SCRIPT_RUNNING). Zero allocation: all JSON is
 * written into the caller-supplied response buffer.
 *
 * Subsystem failures are reported inside the JSON response with
 * return value 0; parser-level failures return negative CLI_ERR_*
 * codes (and still write a JSON error response where possible).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "esp_system.h"

#include "cli_if.h"
#include "ble_if.h"
#include "lua_if.h"
#include "script_if.h"
#include "storage_if.h"
#include "bridge_if.h"
#include "power_if.h"
#include "json_if.h"

#define CLI_FW_VERSION "1.0.0"

#define SCAN_INTERVAL_MIN_MS  10
#define SCAN_INTERVAL_MAX_MS  10000

/* Filter engine owned by the CLI (shared with the scan pipeline via
 * pipeline_set_filter(cli_get_filter_engine())) */
static filter_engine_t s_filter_engine;

/* Write formatted JSON into the response buffer; on truncation write a
 * truncated-error JSON and return CLI_ERR_BUFFER from the caller. */
#define CLI_EMIT(resp, len, ...)                                             \
    do {                                                                     \
        int _n = snprintf((resp), (len), __VA_ARGS__);                       \
        if (_n < 0 || (size_t)_n >= (size_t)(len)) {                         \
            snprintf((resp), (len),                                          \
                "{\"status\":\"error\",\"code\":-903,"                       \
                "\"msg\":\"response truncated\"}");                          \
            return CLI_ERR_BUFFER;                                           \
        }                                                                    \
    } while (0)

static int s_state_error(char *response, uint16_t response_len,
                         const char *cmd_name, const char *why)
{
    CLI_EMIT(response, response_len,
        "{\"status\":\"error\",\"cmd\":\"%s\",\"code\":%d,"
        "\"msg\":\"invalid state: %s\"}",
        cmd_name, CLI_ERR_STATE, why);
    return CLI_ERR_STATE;
}

static int s_syntax_error(char *response, uint16_t response_len,
                          const char *expected)
{
    CLI_EMIT(response, response_len,
        "{\"status\":\"error\",\"msg\":\"invalid syntax: expected %s\"}",
        expected);
    return CLI_ERR_INVALID_CMD;
}

/* Append formatted text at *off; returns 0 or CLI_ERR_BUFFER. */
static int s_append(char *response, uint16_t response_len, int *off,
                    const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int remaining = (int)response_len - *off;
    int n = (remaining > 0)
        ? vsnprintf(response + *off, (size_t)remaining, fmt, ap) : -1;
    va_end(ap);
    if (n < 0 || n >= remaining) {
        snprintf(response, response_len,
            "{\"status\":\"error\",\"code\":-903,"
            "\"msg\":\"response truncated\"}");
        return CLI_ERR_BUFFER;
    }
    *off += n;
    return 0;
}

cli_state_t cli_get_state(void)
{
    if (script_is_running()) return CLI_STATE_SCRIPT_RUNNING;
    if (ble_scan_is_active()) return CLI_STATE_SCANNING;
    return CLI_STATE_IDLE;
}

filter_engine_t *cli_get_filter_engine(void)
{
    return &s_filter_engine;
}

int cli_init(void)
{
    int ret = filter_init(&s_filter_engine);
    if (ret != 0) return ret;
    return 0;
}

/* ---- STATUS / VERSION ------------------------------------------------ */

static int h_status(char *response, uint16_t response_len)
{
    pipeline_stats_t stats;
    pipeline_get_stats(&stats);

    uint32_t free_space = 0;
    storage_get_free_space(&free_space);

    /* H4 observability: system heap + Lua static-pool pressure */
    uint32_t lua_used = 0, lua_peak = 0;
    lua_engine_pool_stats(&lua_used, &lua_peak);

    const char *state_name = "idle";
    cli_state_t st = cli_get_state();
    if (st == CLI_STATE_SCANNING) state_name = "scanning";
    else if (st == CLI_STATE_SCRIPT_RUNNING) state_name = "script_running";

    CLI_EMIT(response, response_len,
        "{\"status\":\"ok\",\"cmd\":\"status\","
        "\"state\":\"%s\","
        "\"reset_reason\":%d,"
        "\"scanning\":%s,"
        "\"queue_drops\":%lu,"
        "\"filter_count\":%d,"
        "\"lua_ready\":%s,"
        "\"script_loaded\":%s,"
        "\"script_running\":%s,"
        "\"free_storage\":%lu,"
        "\"free_heap\":%lu,"
        "\"lua_pool\":{\"used\":%lu,\"peak\":%lu},"
        "\"pipeline\":{\"received\":%lu,\"filtered\":%lu,"
        "\"output\":%lu,\"parse_err\":%lu,\"encode_err\":%lu}}",
        state_name,
        /* Boot observability: how this boot happened (11 = USB reset,
         * which the host triggers by closing the port mid-scan) */
        (int)esp_reset_reason(),
        ble_scan_is_active() ? "true" : "false",
        (unsigned long)ble_scan_get_drop_count(),
        filter_get_count(&s_filter_engine),
        lua_engine_is_ready() ? "true" : "false",
        script_is_loaded() ? "true" : "false",
        script_is_running() ? "true" : "false",
        (unsigned long)free_space,
        (unsigned long)esp_get_free_heap_size(),
        (unsigned long)lua_used,
        (unsigned long)lua_peak,
        (unsigned long)stats.total_received,
        (unsigned long)stats.total_filtered,
        (unsigned long)stats.total_output,
        (unsigned long)stats.parse_errors,
        (unsigned long)stats.encode_errors);
    return 0;
}

static int h_version(char *response, uint16_t response_len)
{
    CLI_EMIT(response, response_len,
        "{\"status\":\"ok\",\"cmd\":\"version\",\"firmware\":\"%s\","
        "\"build_date\":\"%s\",\"chip\":\"esp32s3\"}",
        CLI_FW_VERSION, __DATE__);
    return 0;
}

/* ---- SCAN ------------------------------------------------------------ */

static int h_scan(const char *action, char *response, uint16_t response_len)
{
    cli_state_t st = cli_get_state();

    if (strcmp(action, "START") == 0) {
        if (st == CLI_STATE_SCRIPT_RUNNING)
            return s_state_error(response, response_len, "scan_start",
                                 "stop the script first");
        if (!ble_is_ready()) {
            CLI_EMIT(response, response_len,
                "{\"status\":\"error\",\"cmd\":\"scan_start\","
                "\"msg\":\"BLE not initialized\"}");
            return 0;
        }
        if (st == CLI_STATE_SCANNING) {
            CLI_EMIT(response, response_len,
                "{\"status\":\"error\",\"cmd\":\"scan_start\","
                "\"msg\":\"already scanning\"}");
            return 0;
        }
        int ret1 = ble_scan_start();
        if (ret1 != 0) {
            CLI_EMIT(response, response_len,
                "{\"status\":\"error\",\"cmd\":\"scan_start\","
                "\"msg\":\"scan failed: %d\"}", ret1);
            return 0;
        }
        int ret2 = pipeline_start();
        if (ret2 != 0) {
            /* Rollback: scan started but pipeline failed (B-S3-7 fix) */
            ble_scan_stop();
            CLI_EMIT(response, response_len,
                "{\"status\":\"error\",\"cmd\":\"scan_start\","
                "\"msg\":\"pipeline failed: %d\"}", ret2);
            return 0;
        }
        /* F4.3: block light sleep while streaming — console output is
         * dropped if the SoC sleeps between adv events. */
        power_hold_activity(true);
        CLI_EMIT(response, response_len,
            "{\"status\":\"ok\",\"cmd\":\"scan_start\"}");
        return 0;
    }

    if (strcmp(action, "STOP") == 0) {
        if (st == CLI_STATE_SCRIPT_RUNNING)
            return s_state_error(response, response_len, "scan_stop",
                                 "stop the script first");
        if (st != CLI_STATE_SCANNING) {
            CLI_EMIT(response, response_len,
                "{\"status\":\"error\",\"cmd\":\"scan_stop\","
                "\"msg\":\"not scanning\"}");
            return 0;
        }
        /* L-S4-2 fix: stop results were silently ignored */
        int r1 = pipeline_stop();
        int r2 = ble_scan_stop();
        power_hold_activity(false);
        if (r1 != 0 || r2 != 0) {
            CLI_EMIT(response, response_len,
                "{\"status\":\"error\",\"cmd\":\"scan_stop\","
                "\"msg\":\"stop failed: %d/%d\"}", r1, r2);
            return 0;
        }
        CLI_EMIT(response, response_len,
            "{\"status\":\"ok\",\"cmd\":\"scan_stop\"}");
        return 0;
    }

    if (strncmp(action, "INTERVAL ", 9) == 0) {
        if (st != CLI_STATE_IDLE)
            return s_state_error(response, response_len, "scan_interval",
                                 "stop scanning first");
        char *end = NULL;
        long ms = strtol(action + 9, &end, 10);
        if (end == action + 9 || *end != '\0' ||
            ms < SCAN_INTERVAL_MIN_MS || ms > SCAN_INTERVAL_MAX_MS) {
            CLI_EMIT(response, response_len,
                "{\"status\":\"error\",\"cmd\":\"scan_interval\","
                "\"msg\":\"invalid value: expected %d..%d ms\"}",
                SCAN_INTERVAL_MIN_MS, SCAN_INTERVAL_MAX_MS);
            return 0;
        }
        int ret = ble_scan_set_params((uint32_t)ms, (uint32_t)ms);
        if (ret != 0) {
            CLI_EMIT(response, response_len,
                "{\"status\":\"error\",\"cmd\":\"scan_interval\","
                "\"msg\":\"set params failed: %d\"}", ret);
            return 0;
        }
        CLI_EMIT(response, response_len,
            "{\"status\":\"ok\",\"cmd\":\"scan_interval\",\"value\":%ld}",
            ms);
        return 0;
    }

    return s_syntax_error(response, response_len,
                          "SCAN START|STOP|INTERVAL <ms>");
}

/* ---- FILTER ---------------------------------------------------------- */

static const char *s_filter_type_name(filter_type_t t)
{
    switch (t) {
        case FILTER_TYPE_NAME: return "name";
        case FILTER_TYPE_UUID: return "uuid";
        case FILTER_TYPE_RSSI: return "rssi";
        case FILTER_TYPE_MAC:  return "mac";
        default:               return "unknown";
    }
}

static int h_filter(const char *action, char *response, uint16_t response_len)
{
    cli_state_t st = cli_get_state();

    if (strcmp(action, "CLEAR") == 0) {
        if (st != CLI_STATE_IDLE)
            return s_state_error(response, response_len, "filter_clear",
                                 "stop scanning/script first");
        lua_engine_lock();  /* B-S3-6 fix: protect filter from pipeline */
        filter_clear(&s_filter_engine);
        lua_engine_unlock();
        CLI_EMIT(response, response_len,
            "{\"status\":\"ok\",\"cmd\":\"filter_clear\"}");
        return 0;
    }

    if (strcmp(action, "LIST") == 0) {
        int count = filter_get_count(&s_filter_engine);
        int off = 0;
        int ret = s_append(response, response_len, &off,
            "{\"status\":\"ok\",\"cmd\":\"filter_list\","
            "\"count\":%d,\"filters\":[", count);
        if (ret != 0) return ret;
        for (uint8_t i = 0; i < s_filter_engine.rule_count; i++) {
            const filter_rule_t *r = &s_filter_engine.rules[i];
            if (r->type == FILTER_TYPE_RSSI) {
                ret = s_append(response, response_len, &off,
                    "%s{\"type\":\"rssi\",\"threshold\":%d}",
                    i > 0 ? "," : "", (int)r->rssi_threshold);
            } else {
                /* H1 fix: patterns are user-supplied text */
                char pattern_esc[FILTER_PATTERN_MAX_LEN * 2];
                json_escape_str(r->pattern, pattern_esc, sizeof(pattern_esc));
                ret = s_append(response, response_len, &off,
                    "%s{\"type\":\"%s\",\"pattern\":\"%s\"}",
                    i > 0 ? "," : "", s_filter_type_name(r->type),
                    pattern_esc);
            }
            if (ret != 0) return ret;
        }
        return s_append(response, response_len, &off, "]}");
    }

    if (strncmp(action, "ADD ", 4) == 0) {
        if (st != CLI_STATE_IDLE)
            return s_state_error(response, response_len, "filter_add",
                                 "stop scanning/script first");

        /* FILTER ADD <type> <value> */
        const char *args = action + 4;
        char type_str[16] = {0};
        char value[FILTER_PATTERN_MAX_LEN] = {0};
        int parsed = sscanf(args, "%15s %31s", type_str, value);

        if (parsed < 2) {
            return s_syntax_error(response, response_len,
                                  "FILTER ADD <NAME|UUID|MAC|RSSI> <value>");
        }

        filter_type_t ftype;
        int8_t rssi_val = 0;
        if (strcmp(type_str, "NAME") == 0) {
            ftype = FILTER_TYPE_NAME;
        } else if (strcmp(type_str, "UUID") == 0) {
            ftype = FILTER_TYPE_UUID;
        } else if (strcmp(type_str, "MAC") == 0) {
            ftype = FILTER_TYPE_MAC;
        } else if (strcmp(type_str, "RSSI") == 0) {
            ftype = FILTER_TYPE_RSSI;
            /* Validate range before narrowing to int8_t (M-S3-7 fix —
             * atoi("999") wrapped silently) */
            char *end = NULL;
            long v = strtol(value, &end, 10);
            if (end == value || *end != '\0' || v < -128 || v > 127) {
                CLI_EMIT(response, response_len,
                    "{\"status\":\"error\",\"cmd\":\"filter_add\","
                    "\"msg\":\"RSSI must be a number in -128..127\"}");
                return 0;
            }
            rssi_val = (int8_t)v;
        } else {
            char type_esc[sizeof(type_str) * 2];
            json_escape_str(type_str, type_esc, sizeof(type_esc));
            CLI_EMIT(response, response_len,
                "{\"status\":\"error\",\"cmd\":\"filter_add\","
                "\"msg\":\"unknown type: %s\"}", type_esc);
            return 0;
        }

        lua_engine_lock();  /* B-S3-6 fix */
        int ret = filter_add_rule(&s_filter_engine, ftype,
            ftype == FILTER_TYPE_RSSI ? NULL : value, rssi_val);
        lua_engine_unlock();
        if (ret == 0) {
            /* H1 fix: value is user-supplied text */
            char value_esc[FILTER_PATTERN_MAX_LEN * 2];
            json_escape_str(value, value_esc, sizeof(value_esc));
            CLI_EMIT(response, response_len,
                "{\"status\":\"ok\",\"cmd\":\"filter_add\","
                "\"index\":%d,\"type\":\"%s\",\"value\":\"%s\"}",
                (int)s_filter_engine.rule_count - 1, type_str, value_esc);
        } else {
            CLI_EMIT(response, response_len,
                "{\"status\":\"error\",\"cmd\":\"filter_add\","
                "\"msg\":\"add failed: %d\"}", ret);
        }
        return 0;
    }

    return s_syntax_error(response, response_len,
                          "FILTER ADD|CLEAR|LIST");
}

/* ---- LUA (extension commands, kept beyond the F4.1 spec) -------------- */

static int h_lua(const char *action, char *response, uint16_t response_len)
{
    if (strncmp(action, "EXEC ", 5) == 0) {
        const char *script = action + 5;
        if (!lua_engine_is_ready()) {
            CLI_EMIT(response, response_len,
                "{\"status\":\"error\",\"cmd\":\"lua_exec\","
                "\"msg\":\"Lua engine not initialized\"}");
            return 0;
        }
        char lua_result[LUA_RESULT_MAX_LEN] = {0};
        int ret = lua_engine_exec(script, lua_result, sizeof(lua_result));
        /* H1 fix: Lua results and error messages can contain quotes and
         * control chars ('[string "..."]') — escape before embedding in
         * the JSON response so hosts always receive valid JSON. */
        char result_esc[LUA_RESULT_MAX_LEN * 2];
        json_escape_str(lua_result[0] ? lua_result
                                      : (ret == 0 ? "" : "exec failed"),
                        result_esc, sizeof(result_esc));
        if (ret == 0) {
            CLI_EMIT(response, response_len,
                "{\"status\":\"ok\",\"cmd\":\"lua_exec\","
                "\"result\":\"%s\"}", result_esc);
        } else {
            CLI_EMIT(response, response_len,
                "{\"status\":\"error\",\"cmd\":\"lua_exec\","
                "\"code\":%d,\"msg\":\"%s\"}",
                ret, result_esc);
        }
        return 0;
    }

    if (strcmp(action, "INIT") == 0) {
        int ret = lua_engine_init();
        if (ret == 0) {
            CLI_EMIT(response, response_len,
                "{\"status\":\"ok\",\"cmd\":\"lua_init\"}");
        } else {
            CLI_EMIT(response, response_len,
                "{\"status\":\"error\",\"cmd\":\"lua_init\","
                "\"code\":%d}", ret);
        }
        return 0;
    }

    if (strcmp(action, "DEINIT") == 0) {
        /* B6 fix: refuse deinit while the pipeline may still invoke Lua
         * hooks or wait on the engine lock — lua_engine_deinit deletes the
         * mutex, so tearing the engine down mid-scan races the pipeline
         * task. Stop scanning/scripts first. */
        if (ble_scan_is_active() || script_is_running()) {
            CLI_EMIT(response, response_len,
                "{\"status\":\"error\",\"cmd\":\"lua_deinit\","
                "\"code\":-911,\"msg\":\"stop scanning and scripts first\"}");
            return 0;
        }
        int ret = lua_engine_deinit();
        if (ret == 0) {
            CLI_EMIT(response, response_len,
                "{\"status\":\"ok\",\"cmd\":\"lua_deinit\"}");
        } else {
            CLI_EMIT(response, response_len,
                "{\"status\":\"error\",\"cmd\":\"lua_deinit\","
                "\"code\":%d}", ret);
        }
        return 0;
    }

    return s_syntax_error(response, response_len, "LUA EXEC|INIT|DEINIT");
}

/* ---- SCRIPT ----------------------------------------------------------- */

static int s_hex_nibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int h_script(const char *action, char *response, uint16_t response_len)
{
    cli_state_t st = cli_get_state();

    if (strcmp(action, "BEGIN") == 0) {
        int ret = script_upload_begin();
        if (ret == 0) {
            CLI_EMIT(response, response_len,
                "{\"status\":\"ok\",\"cmd\":\"script_begin\"}");
        } else {
            CLI_EMIT(response, response_len,
                "{\"status\":\"error\",\"cmd\":\"script_begin\","
                "\"code\":%d}", ret);
        }
        return 0;
    }

    if (strcmp(action, "LOAD") == 0) {
        /* F4.2 text-line upload protocol (LLM bridge) */
        return bridge_upload_begin(response, response_len);
    }

    if (strncmp(action, "CHUNK ", 6) == 0) {
        /* SCRIPT CHUNK <hex_data> — validated decode (M-S3-8 fix) */
        const char *hex = action + 6;
        size_t hex_len = strlen(hex);
        uint8_t chunk_buf[512];
        uint16_t chunk_len = 0;
        bool hex_ok = (hex_len > 0) && (hex_len % 2 == 0) &&
                      (hex_len / 2 <= sizeof(chunk_buf));

        if (hex_ok) {
            for (size_t i = 0; i < hex_len; i += 2) {
                int hi = s_hex_nibble(hex[i]);
                int lo = s_hex_nibble(hex[i + 1]);
                if (hi < 0 || lo < 0) {
                    hex_ok = false;
                    break;
                }
                chunk_buf[chunk_len++] = (uint8_t)((hi << 4) | lo);
            }
        }

        if (!hex_ok) {
            CLI_EMIT(response, response_len,
                "{\"status\":\"error\",\"cmd\":\"script_chunk\","
                "\"msg\":\"invalid hex: even length, 0-9a-f, max %u bytes\"}",
                (unsigned)sizeof(chunk_buf));
            return 0;
        }
        int ret = script_upload_chunk(chunk_buf, chunk_len);
        if (ret == 0) {
            CLI_EMIT(response, response_len,
                "{\"status\":\"ok\",\"cmd\":\"script_chunk\","
                "\"bytes\":%d}", chunk_len);
        } else {
            CLI_EMIT(response, response_len,
                "{\"status\":\"error\",\"cmd\":\"script_chunk\","
                "\"code\":%d}", ret);
        }
        return 0;
    }

    if (strcmp(action, "END") == 0) {
        /* SCRIPT END finalizes whichever protocol is active: the F4.2
         * text-line bridge if a SCRIPT LOAD upload is in progress,
         * otherwise the hex-chunk path. */
        if (bridge_is_uploading()) {
            return bridge_upload_finish(response, response_len);
        }
        int ret = script_upload_end(NULL, 0);
        if (ret == 0) {
            CLI_EMIT(response, response_len,
                "{\"status\":\"ok\",\"cmd\":\"script_end\"}");
        } else {
            CLI_EMIT(response, response_len,
                "{\"status\":\"error\",\"cmd\":\"script_end\","
                "\"code\":%d}", ret);
        }
        return 0;
    }

    if (strcmp(action, "RUN") == 0) {
        if (st == CLI_STATE_IDLE)
            return s_state_error(response, response_len, "script_run",
                                 "start scanning first");
        if (st == CLI_STATE_SCRIPT_RUNNING)
            return s_state_error(response, response_len, "script_run",
                                 "already running");
        int ret = script_run();
        if (ret == 0) {
            CLI_EMIT(response, response_len,
                "{\"status\":\"ok\",\"cmd\":\"script_run\"}");
        } else {
            CLI_EMIT(response, response_len,
                "{\"status\":\"error\",\"cmd\":\"script_run\","
                "\"code\":%d}", ret);
        }
        return 0;
    }

    if (strcmp(action, "STOP") == 0) {
        if (st != CLI_STATE_SCRIPT_RUNNING)
            return s_state_error(response, response_len, "script_stop",
                                 "no script running");
        script_stop();
        CLI_EMIT(response, response_len,
            "{\"status\":\"ok\",\"cmd\":\"script_stop\"}");
        return 0;
    }

    if (strcmp(action, "STATUS") == 0) {
        CLI_EMIT(response, response_len,
            "{\"status\":\"ok\",\"cmd\":\"script_status\","
            "\"loaded\":%s,\"running\":%s}",
            script_is_loaded() ? "true" : "false",
            script_is_running() ? "true" : "false");
        return 0;
    }

    return s_syntax_error(response, response_len,
                          "SCRIPT LOAD|BEGIN|CHUNK|END|RUN|STOP|STATUS");
}

/* ---- POWER (extension commands for F4.3 observability/control) --------- */

static int h_power(const char *action, char *response, uint16_t response_len)
{
    if (strcmp(action, "SLEEP ON") == 0) {
        power_enable_sleep(true);
        CLI_EMIT(response, response_len,
            "{\"status\":\"ok\",\"cmd\":\"power_sleep\",\"enabled\":true}");
        return 0;
    }

    if (strcmp(action, "SLEEP OFF") == 0) {
        power_enable_sleep(false);
        CLI_EMIT(response, response_len,
            "{\"status\":\"ok\",\"cmd\":\"power_sleep\",\"enabled\":false}");
        return 0;
    }

    if (strcmp(action, "STATUS") == 0) {
        power_config_t cfg;
        power_get_config(&cfg);
        uint32_t ma = 0;
        power_get_current_ma(&ma);
        const char *state =
            (power_get_state() == POWER_STATE_LIGHT_SLEEP)
            ? "light_sleep" : "active";
        CLI_EMIT(response, response_len,
            "{\"status\":\"ok\",\"cmd\":\"power_status\","
            "\"sleep_enabled\":%s,"
            "\"state\":\"%s\","
            "\"est_current_ma\":%lu}",
            cfg.sleep_enabled ? "true" : "false",
            state, (unsigned long)ma);
        return 0;
    }

    return s_syntax_error(response, response_len,
                          "POWER SLEEP ON|OFF|STATUS");
}

/* ---- Dispatch ---------------------------------------------------------- */

/* True when the line is recognized as a CLI command (as opposed to a
 * script text line arriving during a F4.2 text-line upload). */
static bool s_is_cli_command(const char *cmd)
{
    static const char * const cmds[] = {
        "STATUS", "VERSION", "SCAN", "FILTER", "LUA", "SCRIPT", "POWER", NULL
    };
    for (int i = 0; cmds[i] != NULL; i++) {
        size_t n = strlen(cmds[i]);
        if (strcmp(cmd, cmds[i]) == 0 ||
            (strncmp(cmd, cmds[i], n) == 0 && cmd[n] == ' ')) {
            return true;
        }
    }
    return false;
}

/* ---- Interrupt (Ctrl+C) ------------------------------------------------ */

/* Ctrl+C from the terminal: stop whatever is streaming (upload, script,
 * scan) so a flooded terminal can always be recovered with one key. */
static int h_interrupt(char *response, uint16_t response_len)
{
    if (bridge_is_uploading()) {
        bridge_abort();
    }
    if (script_is_running()) {
        script_stop();
    }
    if (ble_scan_is_active()) {
        int r1 = pipeline_stop();
        int r2 = ble_scan_stop();
        power_hold_activity(false);
        if (r1 != 0 || r2 != 0) {
            CLI_EMIT(response, response_len,
                "{\"status\":\"error\",\"cmd\":\"interrupt\","
                "\"msg\":\"stop failed: %d/%d\"}", r1, r2);
            return 0;
        }
    }
    CLI_EMIT(response, response_len,
        "{\"status\":\"ok\",\"cmd\":\"interrupt\"}");
    return 0;
}

int cli_process_command(const char *cmd, char *response, uint16_t response_len)
{
    if (cmd == NULL || response == NULL || response_len == 0)
        return CLI_ERR_NULL;

    response[0] = '\0';

    /* Ctrl+C (0x03) — immediate interrupt, takes priority over upload
     * mode so a stuck SCRIPT LOAD can always be cancelled. */
    if (cmd[0] == '\x03' && cmd[1] == '\0')
        return h_interrupt(response, response_len);

    /* F4.2 upload mode: every line is script text except SCRIPT END.
     * Any other recognized CLI command aborts the upload and then
     * proceeds normally (AC #4). */
    if (bridge_is_uploading()) {
        if (strcmp(cmd, "SCRIPT END") == 0)
            return h_script("END", response, response_len);
        if (s_is_cli_command(cmd))
            bridge_abort();
        else
            return bridge_handle_script_upload(
                cmd, (uint32_t)strlen(cmd), response, response_len);
    }

    if (strcmp(cmd, "STATUS") == 0)
        return h_status(response, response_len);

    if (strcmp(cmd, "VERSION") == 0)
        return h_version(response, response_len);

    if (strcmp(cmd, "SCAN") == 0)
        return s_syntax_error(response, response_len,
                              "SCAN START|STOP|INTERVAL <ms>");
    if (strncmp(cmd, "SCAN ", 5) == 0)
        return h_scan(cmd + 5, response, response_len);

    if (strcmp(cmd, "FILTER") == 0)
        return s_syntax_error(response, response_len,
                              "FILTER ADD|CLEAR|LIST");
    if (strncmp(cmd, "FILTER ", 7) == 0)
        return h_filter(cmd + 7, response, response_len);

    if (strcmp(cmd, "LUA") == 0)
        return s_syntax_error(response, response_len, "LUA EXEC|INIT|DEINIT");
    if (strncmp(cmd, "LUA ", 4) == 0)
        return h_lua(cmd + 4, response, response_len);

    if (strcmp(cmd, "SCRIPT") == 0)
        return s_syntax_error(response, response_len,
                              "SCRIPT LOAD|BEGIN|CHUNK|END|RUN|STOP|STATUS");
    if (strncmp(cmd, "SCRIPT ", 7) == 0)
        return h_script(cmd + 7, response, response_len);

    if (strcmp(cmd, "POWER") == 0)
        return s_syntax_error(response, response_len,
                              "POWER SLEEP ON|OFF|STATUS");
    if (strncmp(cmd, "POWER ", 6) == 0)
        return h_power(cmd + 6, response, response_len);

    CLI_EMIT(response, response_len,
        "{\"status\":\"error\",\"msg\":\"unknown command\"}");
    return CLI_ERR_INVALID_CMD;
}
