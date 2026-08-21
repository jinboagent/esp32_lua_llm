#include "json_if.h"
#include <stdio.h>
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

/*
 * ========================================================================
 *  F2.4 connection line encoder — shared pure implementation (both builds)
 * ========================================================================
 */

/* Escape like json_escape_str but report whether truncation happened. */
static void s_escape_track(const char *in, char *out, uint16_t out_len,
                           bool *truncated)
{
    *truncated = false;
    if (out_len < 2) {
        out[0] = '\0';
        *truncated = *in != '\0';
        return;
    }
    uint16_t pos = 0;
    for (const unsigned char *p = (const unsigned char *)in; *p != '\0'; p++) {
        char esc_buf[8];
        const char *esc = NULL;

        if (*p == '"') esc = "\\\"";
        else if (*p == '\\') esc = "\\\\";
        else if (*p == '\n') esc = "\\n";
        else if (*p == '\r') esc = "\\r";
        else if (*p == '\t') esc = "\\t";
        else if (*p == '\b') esc = "\\b";
        else if (*p == '\f') esc = "\\f";
        else if (*p < 0x20) {
            snprintf(esc_buf, sizeof(esc_buf), "\\u%04x", (unsigned)*p);
            esc = esc_buf;
        }

        if (esc == NULL) {
            if (pos >= out_len - 1) { *truncated = true; break; }
            out[pos++] = (char)*p;
        } else {
            uint16_t esc_len = (uint16_t)strlen(esc);
            if (pos + esc_len > out_len - 1) { *truncated = true; break; }
            memcpy(out + pos, esc, esc_len);
            pos += esc_len;
        }
    }
    out[pos] = '\0';
}

/* True when s is a JSON object (ws-tolerant, string/escape aware). */
static bool s_is_json_object(const char *s)
{
    const char *p = s;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    if (*p != '{') return false;
    int depth = 0;
    bool in_str = false, esc = false;
    for (; *p != '\0'; p++) {
        unsigned char c = (unsigned char)*p;
        /* Raw control bytes are invalid JSON inside strings (they must
         * be escaped) and, outside strings, only \t \n \r count as
         * whitespace. Object-looking payloads that fail this take the
         * wrap path, so the emitted line never carries a raw newline. */
        if (c < 0x20) {
            if (in_str || (c != '\t' && c != '\n' && c != '\r')) {
                return false;
            }
            continue;
        }
        if (in_str) {
            if (esc) esc = false;
            else if (c == '\\') esc = true;
            else if (c == '"') in_str = false;
            continue;
        }
        if (c == '"') { in_str = true; continue; }
        if (c == '{' || c == '[') depth++;
        else if (c == '}' || c == ']') {
            depth--;
            if (depth == 0) {
                for (p++; *p != '\0'; p++) {
                    unsigned char q = (unsigned char)*p;
                    if (q != ' ' && q != '\t' && q != '\n' && q != '\r')
                        return false;
                }
                return true;
            }
        }
    }
    return false;
}

/* True when a top-level member key of the object collides with an
 * envelope key (ts/addr/src) — D2: envelope wins, so wrap instead. */
static bool s_has_colliding_key(const char *s)
{
    int depth = 0;
    bool in_str = false, esc = false, expect_key = false;
    for (const char *p = s; *p != '\0'; p++) {
        char c = *p;
        if (in_str) {
            if (esc) esc = false;
            else if (c == '\\') esc = true;
            else if (c == '"') in_str = false;
            continue;
        }
        switch (c) {
        case '"':
            if (depth == 1 && expect_key) {
                const char *k = p + 1;
                const char *e = k;
                bool kesc = false;
                while (*e != '\0' && !(*e == '"' && !kesc)) {
                    kesc = (*e == '\\') && !kesc;
                    e++;
                }
                size_t klen = (size_t)(e - k);
                if ((klen == 2 && memcmp(k, "ts", 2) == 0) ||
                    (klen == 4 && memcmp(k, "addr", 4) == 0) ||
                    (klen == 3 && memcmp(k, "src", 3) == 0)) {
                    return true;
                }
                p = e;             /* loop p++ lands past the quote */
                expect_key = false;
            } else {
                in_str = true;
            }
            break;
        case '{': depth++; if (depth == 1) expect_key = true; break;
        case '}': depth--; break;
        case '[': depth++; break;
        case ']': depth--; break;
        case ',': if (depth == 1) expect_key = true; break;
        default: break;
        }
    }
    return false;
}

/* Member list between the outer braces, whitespace-trimmed. */
static void s_object_inner(const char *s, const char **inner, uint16_t *len)
{
    const char *b = strchr(s, '{') + 1;
    int depth = 0;
    bool in_str = false, esc = false;
    const char *e = b;
    for (const char *p = b; *p != '\0'; p++) {
        char c = *p;
        if (in_str) {
            if (esc) esc = false;
            else if (c == '\\') esc = true;
            else if (c == '"') in_str = false;
            continue;
        }
        if (c == '"') { in_str = true; continue; }
        if (c == '{' || c == '[') depth++;
        else if (c == '}' || c == ']') {
            if (depth == 0) { e = p; break; }
            depth--;
        }
    }
    while (b < e && (*b == ' ' || *b == '\t' || *b == '\n' || *b == '\r')) b++;
    while (e > b && (*(e-1) == ' ' || *(e-1) == '\t' || *(e-1) == '\n' || *(e-1) == '\r')) e--;
    *inner = b;
    *len = (uint16_t)(e - b);
}

int json_encode_conn(uint32_t ts_ms, const char *addr, const char *payload,
                     char *buf, uint16_t buf_len, uint16_t *out_len)
{
    if (addr == NULL || payload == NULL || buf == NULL) {
        return -202;
    }
    if (buf_len < 2) {
        return -203;
    }

    /* Merged form: payload object without envelope-key collisions. */
    if (s_is_json_object(payload) && !s_has_colliding_key(payload)) {
        const char *inner = NULL;
        uint16_t inner_len = 0;
        s_object_inner(payload, &inner, &inner_len);
        int n = snprintf(buf, buf_len,
                         "{\"ts\":%lu,\"addr\":\"%s\",\"src\":\"conn\"%s%.*s}",
                         (unsigned long)ts_ms, addr,
                         inner_len > 0 ? "," : "", (int)inner_len, inner);
        if (n > 0 && (uint16_t)n < buf_len) {
            if (out_len != NULL) *out_len = (uint16_t)n;
            return 0;
        }
        /* does not fit — fall through to wrapped form with trunc flag */
    }

    /* Wrapped form: envelope always wins; truncation is marked. */
    char pre[110];
    int pre_n = snprintf(pre, sizeof(pre),
                         "{\"ts\":%lu,\"addr\":\"%s\",\"src\":\"conn\"",
                         (unsigned long)ts_ms, addr);
    if (pre_n < 0 || pre_n >= (int)sizeof(pre)) {
        return -203;
    }
    const uint16_t FLAG_LEN = 13;              /* ,"trunc":true */
    const uint16_t TAIL_LEN = 11;              /* ,"data":""}  */
    if ((uint16_t)pre_n + TAIL_LEN + 2 > buf_len) {
        return -203;
    }
    char esc[JSON_LINE_MAX_LEN];
    bool trunc = false;
    uint16_t room = buf_len - (uint16_t)pre_n - TAIL_LEN;
    if (room >= sizeof(esc)) room = sizeof(esc) - 1;
    s_escape_track(payload, esc, room, &trunc);
    if (trunc) {
        uint16_t room2 = buf_len - (uint16_t)pre_n - TAIL_LEN - FLAG_LEN;
        if (room2 < 2) {
            return -203;
        }
        if (room2 >= sizeof(esc)) room2 = sizeof(esc) - 1;
        s_escape_track(payload, esc, room2, &trunc);
    }
    int n = snprintf(buf, buf_len, "%s%s,\"data\":\"%s\"}",
                     pre, trunc ? ",\"trunc\":true" : "", esc);
    if (n < 0 || (uint16_t)n >= buf_len) {
        return -203;
    }
    if (out_len != NULL) *out_len = (uint16_t)n;
    return 0;
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
            snprintf(esc_buf, sizeof(esc_buf), "\\u%02X", c);
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
