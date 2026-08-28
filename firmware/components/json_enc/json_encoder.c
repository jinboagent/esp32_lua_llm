#include "json_if.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

/*
 * Shared helper (H1 fix): escape dynamic text for safe embedding in a
 * JSON string literal. Pure C — used by both the ESP32 and host builds.
 */
int json_escape_str(const char *in, char *out, uint16_t out_len)
{
    if (in == NULL || out == NULL) {
        return -202;
    }
    if (out_len < 2) {
        return -203;
    }

    uint16_t pos = 0;
    for (const unsigned char *p = (const unsigned char *)in; *p != '\0'; p++) {
        char esc_buf[8];
        const char *esc = NULL;

        if (*p == '"') {
            esc = "\\\"";
        } else if (*p == '\\') {
            esc = "\\\\";
        } else if (*p == '\n') {
            esc = "\\n";
        } else if (*p == '\r') {
            esc = "\\r";
        } else if (*p == '\t') {
            esc = "\\t";
        } else if (*p == '\b') {
            esc = "\\b";
        } else if (*p == '\f') {
            esc = "\\f";
        } else if (*p < 0x20) {
            snprintf(esc_buf, sizeof(esc_buf), "\\u%04x", (unsigned)*p);
            esc = esc_buf;
        }

        if (esc == NULL) {
            if (pos >= out_len - 1) {
                break;  /* truncate; output stays valid JSON */
            }
            out[pos++] = (char)*p;
        } else {
            uint16_t esc_len = (uint16_t)strlen(esc);
            if (pos + esc_len > out_len - 1) {
                break;  /* never split an escape sequence */
            }
            memcpy(out + pos, esc, esc_len);
            pos += esc_len;
        }
    }
    out[pos] = '\0';
    return 0;
}

/* ========================================================================
 * Connection line encoder (F2.4) — shared by ESP32 and host builds.
 * Pure bounded scanner over (payload, payload_len); the payload is NOT
 * assumed to be NUL-terminated.
 * ======================================================================== */

typedef struct {
    char    *buf;
    uint16_t cap;
    uint16_t pos;
    bool     overflow;
} conn_wr_t;

static void s_wr_bytes(conn_wr_t *w, const char *s, uint16_t n)
{
    if (w->overflow) return;
    if ((uint32_t)w->pos + n >= w->cap) {   /* keep room for the NUL */
        w->overflow = true;
        return;
    }
    memcpy(w->buf + w->pos, s, n);
    w->pos = (uint16_t)(w->pos + n);
}

static void s_wr_str(conn_wr_t *w, const char *s)
{
    s_wr_bytes(w, s, (uint16_t)strlen(s));
}

static bool s_wr_fmt(conn_wr_t *w, const char *fmt, ...)
{
    char tmp[48];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= sizeof(tmp)) {
        w->overflow = true;
        return false;
    }
    s_wr_bytes(w, tmp, (uint16_t)n);
    return !w->overflow;
}

static bool s_is_ws(uint8_t c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

/*
 * Is the payload exactly one top-level JSON object (modulo whitespace)?
 * Tracks strings (with escapes) and nesting so braces inside strings or
 * nested values cannot fake a match; anything trailing the closing brace
 * other than whitespace disqualifies it.
 * Returns the index just past '{' in *body_start and the matching '}'
 * index in *body_end when true.
 */
static bool s_conn_payload_is_object(const uint8_t *p, uint16_t len,
                                     uint16_t *body_start, uint16_t *body_end)
{
    uint16_t i = 0;
    while (i < len && s_is_ws(p[i])) i++;
    if (i >= len || p[i] != '{') return false;
    *body_start = (uint16_t)(i + 1);

    int depth = 0;
    bool in_str = false, esc = false;
    for (; i < len; i++) {
        uint8_t c = p[i];
        if (in_str) {
            if (esc) esc = false;
            else if (c == '\\') esc = true;
            else if (c == '"') in_str = false;
            continue;
        }
        if (c == '"') in_str = true;
        else if (c == '{') depth++;
        else if (c == '}') {
            depth--;
            if (depth == 0) {
                *body_end = i;
                i++;
                while (i < len && s_is_ws(p[i])) i++;
                return i == len;
            }
        }
    }
    return false;
}

/* True when [i..end) is exactly the quoted JSON string "key". */
static bool s_conn_key_is(const uint8_t *p, uint16_t i, uint16_t end,
                          const char *key)
{
    if (i >= end || p[i] != '"') return false;
    uint16_t j = (uint16_t)(i + 1);
    const uint8_t *k = (const uint8_t *)key;
    while (j < end && *k != '\0') {
        if (p[j] != *k) return false;
        j++;
        k++;
    }
    return *k == '\0' && j < end && p[j] == '"';
}

/*
 * Validate a JSON escape sequence whose escape character is p[i]
 * (p[i-1] was the backslash). Returns the extra bytes consumed after
 * the escape character (0 for simple escapes, 4 for \uXXXX), or -1
 * when the escape is invalid. Merged members are copied VERBATIM, so
 * an invalid escape (e.g. \x from a non-JSON payload) or a raw control
 * byte must abort the merge and fall back to wrapping, which escapes
 * everything safely (found by test_fuzz, 2026-08-29).
 */
static int s_conn_escape_len(const uint8_t *p, uint16_t i, uint16_t end)
{
    uint8_t c = p[i];
    if (c == '"' || c == '\\' || c == '/' || c == 'b' || c == 'f' ||
        c == 'n' || c == 'r' || c == 't') {
        return 0;
    }
    if (c == 'u') {
        if (i + 4 >= end) return -1;      /* need 4 hex digits in range */
        for (int k = 1; k <= 4; k++) {
            uint8_t h = p[i + k];
            if (!((h >= '0' && h <= '9') || (h >= 'a' && h <= 'f') ||
                  (h >= 'A' && h <= 'F'))) {
                return -1;
            }
        }
        return 4;
    }
    return -1;
}

/*
 * Append one top-level member (key through value end) to the output,
 * unless its key collides with an envelope key (envelope wins, review Q3).
 * The member scanner validates the shape "string":value as it goes; any
 * malformed member aborts the merge (caller falls back to wrapping).
 * Returns 0 ok, 1 member skipped (envelope key), -1 malformed payload.
 */
static int s_conn_append_member(conn_wr_t *w, const uint8_t *p,
                                uint16_t key_start, uint16_t val_end)
{
    /* key: quoted string starting at key_start */
    if (key_start >= val_end || p[key_start] != '"') return -1;
    uint16_t i = (uint16_t)(key_start + 1);
    bool in_str = true, esc = false;
    while (i < val_end) {
        uint8_t c = p[i];
        if (esc) {
            int elen = s_conn_escape_len(p, i, val_end);
            if (elen < 0) return -1;
            i = (uint16_t)(i + 1 + elen);
            esc = false;
            continue;
        }
        if (c == '\\') { esc = true; i++; continue; }
        if (c == '"') { in_str = false; i++; break; }
        if (c < 0x20) return -1;          /* raw control char */
        i++;
    }
    if (in_str) return -1;                  /* unterminated key */

    while (i < val_end && s_is_ws(p[i])) i++;
    if (i >= val_end || p[i] != ':') return -1;
    i++;
    while (i < val_end && s_is_ws(p[i])) i++;

    /* value: string / object / array / number / literal */
    uint8_t c0 = (i < val_end) ? p[i] : 0;
    if (c0 == '"') {
        i++;                               /* skip the opening quote */
        in_str = true; esc = false;
        while (i < val_end) {
            uint8_t c = p[i];
            if (esc) {
                int elen = s_conn_escape_len(p, i, val_end);
                if (elen < 0) return -1;
                i = (uint16_t)(i + 1 + elen);
                esc = false;
                continue;
            }
            if (c == '\\') { esc = true; i++; continue; }
            if (c == '"') { i++; in_str = false; break; }
            if (c < 0x20) return -1;
            i++;
        }
        if (in_str) return -1;
    } else if (c0 == '{' || c0 == '[') {
        uint8_t open = c0, close = (c0 == '{') ? '}' : ']';
        int depth = 0; in_str = false; esc = false;
        while (i < val_end) {
            uint8_t c = p[i];
            if (in_str) {
                if (esc) {
                    int elen = s_conn_escape_len(p, i, val_end);
                    if (elen < 0) return -1;
                    i = (uint16_t)(i + 1 + elen);
                    esc = false;
                    continue;
                }
                if (c == '\\') { esc = true; i++; continue; }
                if (c == '"') in_str = false;
                else if (c < 0x20) return -1;
            } else if (c == '"') in_str = true;
            else if (c == open) depth++;
            else if (c == close) {
                depth--;
                if (depth == 0) { i++; break; }
            }
            i++;
        }
        if (depth != 0) return -1;
    } else if (c0 == 't' || c0 == 'f' || c0 == 'n') {
        /* true / false / null — accept the exact literal */
        const char *lit = (c0 == 't') ? "true" : (c0 == 'f') ? "false" : "null";
        for (const uint8_t *l = (const uint8_t *)lit; *l; l++, i++) {
            if (i >= val_end || p[i] != *l) return -1;
        }
    } else if (c0 == '-' || (c0 >= '0' && c0 <= '9')) {
        while (i < val_end) {
            uint8_t c = p[i];
            if (!((c >= '0' && c <= '9') || c == '-' || c == '+' ||
                  c == '.' || c == 'e' || c == 'E')) break;
            i++;
        }
    } else {
        return -1;                          /* empty or unknown value */
    }

    /* Skip the whole member when its key collides with the envelope. */
    if (s_conn_key_is(p, key_start, val_end, "ts") ||
        s_conn_key_is(p, key_start, val_end, "addr") ||
        s_conn_key_is(p, key_start, val_end, "src")) {
        return 1;
    }

    s_wr_bytes(w, (const char *)(p + key_start),
               (uint16_t)(i - key_start));
    return w->overflow ? -1 : 0;
}

/*
 * Append ,"data":"<escaped payload>" in wrap mode with staged budgets so
 * a truncated string can always be closed: payload bytes may fill up to
 * cap-4, the closing quote lands at cap-3 (its writer allows cap-2), and
 * the caller's '}' then fits at cap-2 under the strict write check
 * (pos + n < cap). Returns whether ALL payload bytes were written.
 */
static bool s_conn_write_data_value(conn_wr_t *w,
                                    const uint8_t *p, uint16_t len)
{
    conn_wr_t body = *w;
    body.cap = (uint16_t)(w->cap - 3);

    s_wr_str(&body, ",\"data\":\"");
    /* The closing quote is only constructible when the opening was
     * written: if the body overflowed before/at the opening (tight
     * trunc-path caps) an unconditional quote dangles after
     * "trunc":true; if the body truncated MID-payload the staged
     * budget (payload stops at cap-4) always leaves room for it
     * (both directions found by test_fuzz, 2026-08-29). */
    bool opened = !body.overflow;
    uint16_t i = 0;
    for (; i < len; i++) {
        uint8_t c = p[i];
        char esc[8];
        const char *e = NULL;
        if (c == '"') e = "\\\"";
        else if (c == '\\') e = "\\\\";
        else if (c == '\n') e = "\\n";
        else if (c == '\r') e = "\\r";
        else if (c == '\t') e = "\\t";
        else if (c < 0x20) {
            snprintf(esc, sizeof(esc), "\\u%04x", c);
            e = esc;
        }
        if (e != NULL) s_wr_str(&body, e);
        else s_wr_bytes(&body, (const char *)&c, 1);
        if (body.overflow) {
            break;
        }
    }
    bool all = (i == len) && !body.overflow;

    conn_wr_t quote = *w;
    quote.cap = (uint16_t)(w->cap - 2);
    quote.pos = body.pos;                   /* budget stop is controlled */
    if (opened) {
        s_wr_str(&quote, "\"");             /* fits by construction */
    }
    w->pos = quote.pos;
    w->overflow = quote.overflow;
    return all;
}

int json_encode_conn(const char *addr_str, uint32_t ts_ms,
                     const uint8_t *payload, uint16_t payload_len,
                     char *buf, uint16_t buf_len, uint16_t *out_len)
{
    if (buf == NULL || addr_str == NULL || (payload == NULL && payload_len != 0)) {
        return -202;
    }
    if (buf_len < 2) {
        return -203;
    }

    conn_wr_t w = { .buf = buf, .cap = buf_len, .pos = 0, .overflow = false };

    /* Envelope */
    s_wr_str(&w, "{\"ts\":");
    s_wr_fmt(&w, "%lu", (unsigned long)ts_ms);
    s_wr_str(&w, ",\"addr\":\"");
    s_wr_str(&w, addr_str);
    s_wr_str(&w, "\",\"src\":\"conn\"");
    /* The envelope is the floor: if it did not fit, no fallback can
     * yield valid JSON. The trunc path used to splice ,"trunc":true
     * into a half-written envelope and still return 0 (found by
     * test_fuzz probing cap-1/cap/cap+1, 2026-08-29). */
    if (w.overflow) {
        return -203;
    }

    /* Merge attempt: payload is a JSON object */
    uint16_t body_start = 0, body_end = 0;
    if (s_conn_payload_is_object(payload, payload_len, &body_start, &body_end)) {
        conn_wr_t mw = { .buf = buf, .cap = buf_len, .pos = w.pos,
                         .overflow = false };
        bool merge_ok = true;
        uint16_t i = body_start;
        while (i < body_end) {
            while (i < body_end && (s_is_ws(payload[i]) || payload[i] == ',')) i++;
            if (i >= body_end) break;
            /* find this member's end: scan to the next top-level comma */
            uint16_t j = i;
            int depth = 0; bool in_str = false, esc = false;
            while (j < body_end) {
                uint8_t c = payload[j];
                if (in_str) {
                    if (esc) esc = false;
                    else if (c == '\\') esc = true;
                    else if (c == '"') in_str = false;
                } else if (c == '"') in_str = true;
                else if (c == '{' || c == '[') depth++;
                else if (c == '}' || c == ']') depth--;
                else if (c == ',' && depth == 0) break;
                j++;
            }
            /* Each merged member needs its separating comma — the
             * envelope ends without one (2026-08-28 fix: merge output
             * was invalid JSON, e.g. "src":"conn""v":0). Roll the comma
             * back when the member is skipped for an envelope-key
             * collision, so no dangling separator remains. */
            uint16_t pos_before = mw.pos;
            bool ovf_before = mw.overflow;
            s_wr_str(&mw, ",");
            int rc = s_conn_append_member(&mw, payload, i, j);
            if (rc == 1) {
                mw.pos = pos_before;
                mw.overflow = ovf_before;
            } else if (rc < 0) {
                merge_ok = false; break;
            }
            i = j;
        }
        if (merge_ok && !mw.overflow) {
            s_wr_str(&mw, "}");
            if (!mw.overflow) {
                buf[mw.pos] = '\0';
                if (out_len != NULL) *out_len = mw.pos;
                return 0;
            }
        }
        w.overflow = false;                 /* fall through to wrap */
    }

    /* Wrap attempt: full payload as a data string. The data writer
     * reserves its own closing budget, so the final '}' always fits. */
    {
        conn_wr_t ww = w;                   /* copy: envelope already written */
        bool all = s_conn_write_data_value(&ww, payload, payload_len);
        if (all && !ww.overflow) {
            s_wr_str(&ww, "}");
            if (!ww.overflow) {
                buf[ww.pos] = '\0';
                if (out_len != NULL) *out_len = ww.pos;
                return 0;
            }
        }
    }

    /* Truncation fallback: as many data bytes as fit + "trunc":true —
     * still valid, still one line, downstream can tell it is cut. */
    {
        conn_wr_t tw = w;
        s_wr_str(&tw, ",\"trunc\":true");
        (void)s_conn_write_data_value(&tw, payload, payload_len);
        s_wr_str(&tw, "}");
        if (!tw.overflow) {
            buf[tw.pos] = '\0';
            if (out_len != NULL) *out_len = tw.pos;
            return 0;
        }
    }

    return -203;                            /* buffer below the envelope floor */
}

#ifdef ESP_PLATFORM
/*
 * ========================================================================
 *  ESP32 Implementation: cJSON (ESP-IDF built-in)
 *  Uses cJSON for correct, safe JSON generation. Accepts malloc/free
 *  trade-off for correctness guarantees.
 * ========================================================================
 */

#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>

static void s_add_mac(cJSON *root, const char *key, const uint8_t addr[6])
{
    char mac_str[18];
    snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
             addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);
    cJSON_AddStringToObject(root, key, mac_str);
}

static void s_add_manu_data(cJSON *root, const proto_adv_report_t *report)
{
    if (!report->has_manu) {
        cJSON_AddNullToObject(root, "manu");
        return;
    }

    cJSON *manu = cJSON_CreateObject();
    char id_str[5];
    snprintf(id_str, sizeof(id_str), "%04X", report->manu_id);
    cJSON_AddStringToObject(manu, "id", id_str);

    /* Hex-encode manufacturer data */
    char data_hex[PROTO_MANU_DATA_MAX_LEN * 2 + 1];
    for (uint8_t i = 0; i < report->manu_len; i++) {
        snprintf(&data_hex[i * 2], 3, "%02X", report->manu_data[i]);
    }
    data_hex[report->manu_len * 2] = '\0';
    cJSON_AddStringToObject(manu, "data", data_hex);

    cJSON_AddItemToObject(root, "manu", manu);
}

int json_encode_adv(const proto_adv_report_t *report,
                    char *buf, uint16_t buf_len,
                    uint16_t *out_len)
{
    if (report == NULL || buf == NULL) {
        return -202;
    }
    if (buf_len < 2) {
        return -203;
    }

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return -200;
    }

    cJSON_AddNumberToObject(root, "ts", (double)report->ts_ms);
    s_add_mac(root, "addr", report->addr);
    cJSON_AddStringToObject(root, "type",
        report->addr_type == PROTO_ADDR_TYPE_PUBLIC ? "public" : "random");
    cJSON_AddNumberToObject(root, "rssi", (int)report->rssi);

    if (report->has_name) {
        cJSON_AddStringToObject(root, "name", report->name);
    } else {
        cJSON_AddNullToObject(root, "name");
    }

    /* UUID16 array */
    cJSON *uuids = cJSON_CreateArray();
    for (uint8_t i = 0; i < report->uuid16_count; i++) {
        char uuid_str[5];
        snprintf(uuid_str, sizeof(uuid_str), "%04X", report->uuid16_list[i]);
        cJSON_AddItemToArray(uuids, cJSON_CreateString(uuid_str));
    }
    cJSON_AddItemToObject(root, "uuids", uuids);

    s_add_manu_data(root, report);

    if (report->has_tx_power) {
        cJSON_AddNumberToObject(root, "tx_power", (int)report->tx_power);
    }

    if (report->has_flags) {
        char flags_str[3];
        snprintf(flags_str, sizeof(flags_str), "%02X", report->flags);
        cJSON_AddStringToObject(root, "flags", flags_str);
    }

    /* Serialize to compact JSON (no whitespace) */
    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (json_str == NULL) {
        return -200;
    }

    uint16_t len = (uint16_t)strlen(json_str);
    if (len >= buf_len) {
        free(json_str);
        return -203;
    }

    memcpy(buf, json_str, len + 1);
    free(json_str);

    if (out_len != NULL) {
        *out_len = len;
    }
    return 0;
}

#else
/*
 * ========================================================================
 *  Host Test Implementation: Simple snprintf-based encoder
 *  Used when compiling on PC (no cJSON available).
 *  Same output format as cJSON version for test compatibility.
 * ========================================================================
 */

#include <stdio.h>

static int s_encode_mac(char *buf, uint16_t buf_len, const uint8_t addr[6])
{
    int n = snprintf(buf, buf_len, "%02X:%02X:%02X:%02X:%02X:%02X",
                     addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);
    if (n < 0 || (uint16_t)n >= buf_len) return -203;
    return n;
}

static int s_encode_string_escaped(char *buf, uint16_t buf_len, const char *str)
{
    uint16_t pos = 0;
    for (const char *p = str; *p != '\0'; p++) {
        unsigned char c = (unsigned char)*p;
        const char *esc = NULL;
        char esc_buf[8];

        if (c == '"') { esc = "\\\""; }
        else if (c == '\\') { esc = "\\\\"; }
        else if (c == '\n') { esc = "\\n"; }
        else if (c == '\r') { esc = "\\r"; }
        else if (c == '\t') { esc = "\\t"; }
        else if (c == '\b') { esc = "\\b"; }
        else if (c == '\f') { esc = "\\f"; }
        else if (c < 0x20) {
            /* JSON \u escapes are exactly 4 hex digits (RFC 8259) —
             * %02X emitted 2-digit escapes that made the whole line
             * unparseable (found by test_fuzz, 2026-08-29). */
            snprintf(esc_buf, sizeof(esc_buf), "\\u%04X", c);
            esc = esc_buf;
        }

        if (esc) {
            uint16_t esc_len = (uint16_t)strlen(esc);
            if (pos + esc_len >= buf_len) break;
            memcpy(buf + pos, esc, esc_len);
            pos += esc_len;
        } else {
            if (pos + 1 >= buf_len) break;
            buf[pos++] = (char)c;
        }
    }
    buf[pos] = '\0';
    return (int)pos;
}

int json_encode_adv(const proto_adv_report_t *report,
                    char *buf, uint16_t buf_len,
                    uint16_t *out_len)
{
    if (report == NULL || buf == NULL) return -202;
    if (buf_len < 2) return -203;

    uint16_t pos = 0;
    int n;

    n = snprintf(buf + pos, buf_len - pos, "{\"ts\":%lu", (unsigned long)report->ts_ms);
    if (n < 0 || (uint16_t)n >= buf_len - pos) return -203;
    pos += (uint16_t)n;

    char mac_str[18];
    s_encode_mac(mac_str, sizeof(mac_str), report->addr);
    n = snprintf(buf + pos, buf_len - pos, ",\"addr\":\"%s\"", mac_str);
    if (n < 0 || (uint16_t)n >= buf_len - pos) return -203;
    pos += (uint16_t)n;

    n = snprintf(buf + pos, buf_len - pos, ",\"type\":\"%s\"",
                 report->addr_type == PROTO_ADDR_TYPE_PUBLIC ? "public" : "random");
    if (n < 0 || (uint16_t)n >= buf_len - pos) return -203;
    pos += (uint16_t)n;

    n = snprintf(buf + pos, buf_len - pos, ",\"rssi\":%d", (int)report->rssi);
    if (n < 0 || (uint16_t)n >= buf_len - pos) return -203;
    pos += (uint16_t)n;

    if (report->has_name) {
        char escaped_name[PROTO_DEVICE_NAME_MAX_LEN * 6 + 1];
        s_encode_string_escaped(escaped_name, sizeof(escaped_name), report->name);
        n = snprintf(buf + pos, buf_len - pos, ",\"name\":\"%s\"", escaped_name);
    } else {
        n = snprintf(buf + pos, buf_len - pos, ",\"name\":null");
    }
    if (n < 0 || (uint16_t)n >= buf_len - pos) return -203;
    pos += (uint16_t)n;

    n = snprintf(buf + pos, buf_len - pos, ",\"uuids\":[");
    if (n < 0 || (uint16_t)n >= buf_len - pos) return -203;
    pos += (uint16_t)n;

    for (uint8_t i = 0; i < report->uuid16_count; i++) {
        if (i > 0) {
            n = snprintf(buf + pos, buf_len - pos, ",");
            if (n < 0 || (uint16_t)n >= buf_len - pos) return -203;
            pos += (uint16_t)n;
        }
        n = snprintf(buf + pos, buf_len - pos, "\"%04X\"", report->uuid16_list[i]);
        if (n < 0 || (uint16_t)n >= buf_len - pos) return -203;
        pos += (uint16_t)n;
    }

    n = snprintf(buf + pos, buf_len - pos, "]");
    if (n < 0 || (uint16_t)n >= buf_len - pos) return -203;
    pos += (uint16_t)n;

    if (report->has_manu) {
        n = snprintf(buf + pos, buf_len - pos, ",\"manu\":{\"id\":\"%04X\",\"data\":\"", report->manu_id);
        if (n < 0 || (uint16_t)n >= buf_len - pos) return -203;
        pos += (uint16_t)n;
        for (uint8_t i = 0; i < report->manu_len; i++) {
            n = snprintf(buf + pos, buf_len - pos, "%02X", report->manu_data[i]);
            if (n < 0 || (uint16_t)n >= buf_len - pos) return -203;
            pos += (uint16_t)n;
        }
        n = snprintf(buf + pos, buf_len - pos, "\"}");
        if (n < 0 || (uint16_t)n >= buf_len - pos) return -203;
        pos += (uint16_t)n;
    } else {
        n = snprintf(buf + pos, buf_len - pos, ",\"manu\":null");
        if (n < 0 || (uint16_t)n >= buf_len - pos) return -203;
        pos += (uint16_t)n;
    }

    if (report->has_tx_power) {
        n = snprintf(buf + pos, buf_len - pos, ",\"tx_power\":%d", (int)report->tx_power);
        if (n < 0 || (uint16_t)n >= buf_len - pos) return -203;
        pos += (uint16_t)n;
    }

    if (report->has_flags) {
        n = snprintf(buf + pos, buf_len - pos, ",\"flags\":\"%02X\"", report->flags);
        if (n < 0 || (uint16_t)n >= buf_len - pos) return -203;
        pos += (uint16_t)n;
    }

    n = snprintf(buf + pos, buf_len - pos, "}");
    if (n < 0 || (uint16_t)n >= buf_len - pos) return -203;
    pos += (uint16_t)n;

    if (out_len != NULL) *out_len = pos;
    return 0;
}

#endif /* ESP_PLATFORM */
