/*
 * LLM Script Bridge (F4.2) — lua_llm_bridge.c
 *
 * Text-line upload protocol for LLM-generated Lua scripts. Owns the
 * protocol state and the per-line sandbox scan; delegates buffering,
 * trial-compile and persistence to script_mgmt (the same validated
 * deployment path the hex-chunk protocol uses).
 *
 * Zero allocation: no malloc/free anywhere in this module.
 */

#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "bridge_if.h"
#include "script_if.h"
#include "lua_if.h"
#include "json_if.h"

/* ---- Upload State ---- */

static bridge_state_t s_state = BRIDGE_STATE_IDLE;
static uint32_t       s_bytes = 0;   /* bytes accumulated (incl. newlines) */

/* Usable capacity: script_mgmt reserves one byte for the terminator. */
#define BRIDGE_CAPACITY  ((uint32_t)(SCRIPT_MAX_SIZE - 1))

/* ---- Sandbox scan (AC #7) ----
 *
 * Token-based, fail-closed. Dotted tokens (os., io., ...) can only
 * appear as table indexing, so a plain substring match is safe. Bare
 * identifiers need a word-boundary check so e.g. "dofile" matches but
 * "updofile" would not. A token inside a string literal or comment is
 * rejected too — deliberate fail-closed behaviour for untrusted input.
 */

static const char *s_forbidden_dotted[] = {
    "os.", "io.", "debug.", "package.", NULL
};

static const char *s_forbidden_words[] = {
    "dofile", "loadfile", "load", "require", "collectgarbage", NULL
};

static bool s_is_ident_char(char c)
{
    return (bool)(isalnum((unsigned char)c) || c == '_');
}

/* Returns the forbidden token found in line, or NULL if clean. */
static const char *s_scan_line(const char *line, size_t len)
{
    for (int i = 0; s_forbidden_dotted[i] != NULL; i++) {
        const char *tok = s_forbidden_dotted[i];
        /* Scan only the line's own bytes (line may not be NUL-terminated
         * at len when it comes straight from the USB buffer). */
        const char *end = line + len;
        size_t tok_len = strlen(tok);
        for (const char *p = line; p + tok_len <= end; p++) {
            if (memcmp(p, tok, tok_len) == 0) {
                return tok;
            }
        }
    }

    for (int i = 0; s_forbidden_words[i] != NULL; i++) {
        const char *tok = s_forbidden_words[i];
        const char *end = line + len;
        size_t tok_len = strlen(tok);
        for (const char *p = line; p + tok_len <= end; p++) {
            if (memcmp(p, tok, tok_len) != 0) continue;
            bool left_ok  = (p == line) || !s_is_ident_char(p[-1]);
            bool right_ok = (p + tok_len >= end) ||
                            !s_is_ident_char(p[tok_len]);
            if (left_ok && right_ok) {
                return tok;
            }
        }
    }

    return NULL;
}

/* ---- Helpers ---- */

static void s_reset(void)
{
    script_upload_abort();
    s_state = BRIDGE_STATE_IDLE;
    s_bytes = 0;
}

/* ---- Public API ---- */

int bridge_init(void)
{
    s_state = BRIDGE_STATE_IDLE;
    s_bytes = 0;
    return 0;
}

bool bridge_is_uploading(void)
{
    return s_state == BRIDGE_STATE_UPLOADING;
}

bridge_state_t bridge_get_state(void)
{
    if (s_state == BRIDGE_STATE_VALIDATED && script_is_running()) {
        return BRIDGE_STATE_RUNNING;
    }
    return s_state;
}

void bridge_abort(void)
{
    if (s_state == BRIDGE_STATE_UPLOADING) {
        s_reset();
        printf("Bridge: upload aborted\n");
    }
}

int bridge_upload_begin(char *response, uint16_t response_len)
{
    if (response == NULL || response_len == 0) {
        return BRIDGE_ERR_NULL;
    }

    if (s_state == BRIDGE_STATE_UPLOADING) {
        snprintf(response, response_len,
            "{\"status\":\"error\",\"cmd\":\"script_load\","
            "\"msg\":\"already uploading\"}");
        return BRIDGE_ERR_STATE;
    }

    int ret = script_upload_begin();
    if (ret != 0) {
        snprintf(response, response_len,
            "{\"status\":\"error\",\"cmd\":\"script_load\","
            "\"code\":%d,\"msg\":\"upload session busy\"}", ret);
        return ret;
    }

    s_state = BRIDGE_STATE_UPLOADING;
    s_bytes = 0;
    snprintf(response, response_len,
        "{\"status\":\"ok\",\"cmd\":\"script_load\",\"msg\":\"ready\"}");
    return 0;
}

int bridge_handle_script_upload(const char *script_text, uint32_t len,
                                char *response, uint16_t response_len)
{
    if (script_text == NULL || response == NULL || response_len == 0) {
        return BRIDGE_ERR_NULL;
    }
    response[0] = '\0';  /* data lines are acknowledged silently */

    if (s_state != BRIDGE_STATE_UPLOADING) {
        snprintf(response, response_len,
            "{\"status\":\"error\",\"cmd\":\"script_data\","
            "\"code\":%d,\"msg\":\"no upload in progress\"}",
            BRIDGE_ERR_STATE);
        return BRIDGE_ERR_STATE;
    }

    /* Sandbox scan before the bytes enter the buffer (fail-closed). */
    const char *bad = s_scan_line(script_text, len);
    if (bad != NULL) {
        snprintf(response, response_len,
            "{\"status\":\"error\",\"cmd\":\"script_data\","
            "\"code\":%d,\"msg\":\"sandbox violation: '%s' is not allowed\"}",
            BRIDGE_ERR_REJECTED, bad);
        s_reset();
        return BRIDGE_ERR_REJECTED;
    }

    /* Budget: line + newline, one byte terminator reserved downstream. */
    if (s_bytes + len + 1 > BRIDGE_CAPACITY) {
        snprintf(response, response_len,
            "{\"status\":\"error\",\"cmd\":\"script_data\","
            "\"code\":%d,\"msg\":\"script exceeds %u bytes\"}",
            BRIDGE_ERR_TOO_BIG, (unsigned)BRIDGE_CAPACITY);
        s_reset();
        return BRIDGE_ERR_TOO_BIG;
    }

    int ret = script_upload_chunk((const uint8_t *)script_text,
                                  (uint16_t)len);
    if (ret == 0) {
        ret = script_upload_chunk((const uint8_t *)"\n", 1);
    }
    if (ret != 0) {
        int code = (ret == -603) ? BRIDGE_ERR_TOO_BIG : ret;
        snprintf(response, response_len,
            "{\"status\":\"error\",\"cmd\":\"script_data\","
            "\"code\":%d,\"msg\":\"upload failed\"}", code);
        s_reset();
        return code;
    }

    s_bytes += len + 1;
    return 0;
}

int bridge_upload_finish(char *response, uint16_t response_len)
{
    if (response == NULL || response_len == 0) {
        return BRIDGE_ERR_NULL;
    }

    if (s_state != BRIDGE_STATE_UPLOADING) {
        snprintf(response, response_len,
            "{\"status\":\"error\",\"cmd\":\"script_end\","
            "\"code\":%d,\"msg\":\"no upload in progress\"}",
            BRIDGE_ERR_STATE);
        return BRIDGE_ERR_STATE;
    }

    uint32_t size = s_bytes;
    s_state = BRIDGE_STATE_IDLE;
    s_bytes = 0;

    if (size == 0) {
        script_upload_abort();
        snprintf(response, response_len,
            "{\"status\":\"error\",\"cmd\":\"script_end\","
            "\"code\":%d,\"msg\":\"empty script\"}", BRIDGE_ERR_REJECTED);
        return BRIDGE_ERR_REJECTED;
    }

    char err_buf[128] = {0};
    int ret = script_upload_end(err_buf, sizeof(err_buf));
    if (ret == 0) {
        s_state = BRIDGE_STATE_VALIDATED;
        snprintf(response, response_len,
            "{\"status\":\"ok\",\"cmd\":\"script_end\",\"size\":%lu}",
            (unsigned long)size);
        return 0;
    }

    const char *msg = (ret == -612)
        ? (err_buf[0] ? err_buf : "compile error")
        : (ret == -704 ? "storage write failed" : "upload end failed");
    /* H1 fix: Lua compile errors contain quotes ('[string "..."]') —
     * escape so the host always receives valid JSON. */
    char msg_esc[sizeof(err_buf) * 2];
    json_escape_str(msg, msg_esc, sizeof(msg_esc));
    snprintf(response, response_len,
        "{\"status\":\"error\",\"cmd\":\"script_end\","
        "\"code\":%d,\"msg\":\"%s\"}", ret, msg_esc);
    return ret;
}
