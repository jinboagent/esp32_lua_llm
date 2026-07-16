#include "json_if.h"
#include <stdio.h>
#include <string.h>

static int s_encode_mac(char *buf, uint16_t buf_len, const uint8_t addr[6])
{
    int n = snprintf(buf, buf_len, "%02X:%02X:%02X:%02X:%02X:%02X",
                     addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);
    if (n < 0 || (uint16_t)n >= buf_len) {
        return -203;
    }
    return n;
}

static int s_encode_string_escaped(char *buf, uint16_t buf_len, const char *str)
{
    uint16_t pos = 0;
    for (const char *p = str; *p != '\0' && pos + 2 < buf_len; p++) {
        char c = *p;
        if (c == '"' || c == '\\') {
            buf[pos++] = '\\';
            buf[pos++] = c;
        } else if (c == '\n') {
            buf[pos++] = '\\';
            buf[pos++] = 'n';
        } else if (c == '\r') {
            buf[pos++] = '\\';
            buf[pos++] = 'r';
        } else if (c == '\t') {
            buf[pos++] = '\\';
            buf[pos++] = 't';
        } else if ((unsigned char)c < 0x20) {
            int n = snprintf(&buf[pos], buf_len - pos, "\\u%04x", (unsigned char)c);
            if (n < 0 || (uint16_t)n >= buf_len - pos) {
                return -203;
            }
            pos += (uint16_t)n;
        } else {
            buf[pos++] = c;
        }
    }
    buf[pos] = '\0';
    return (int)pos;
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

    uint16_t pos = 0;
    int n;

    /* opening brace */
    n = snprintf(buf + pos, buf_len - pos, "{\"ts\":%lu",
                 (unsigned long)report->ts_ms);
    if (n < 0 || (uint16_t)n >= buf_len - pos) return -203;
    pos += (uint16_t)n;

    /* addr */
    char mac_str[18];
    s_encode_mac(mac_str, sizeof(mac_str), report->addr);
    n = snprintf(buf + pos, buf_len - pos, ",\"addr\":\"%s\"", mac_str);
    if (n < 0 || (uint16_t)n >= buf_len - pos) return -203;
    pos += (uint16_t)n;

    /* type */
    n = snprintf(buf + pos, buf_len - pos, ",\"type\":\"%s\"",
                 report->addr_type == PROTO_ADDR_TYPE_PUBLIC ? "public" : "random");
    if (n < 0 || (uint16_t)n >= buf_len - pos) return -203;
    pos += (uint16_t)n;

    /* rssi */
    n = snprintf(buf + pos, buf_len - pos, ",\"rssi\":%d", (int)report->rssi);
    if (n < 0 || (uint16_t)n >= buf_len - pos) return -203;
    pos += (uint16_t)n;

    /* name */
    if (report->has_name) {
        char escaped_name[PROTO_DEVICE_NAME_MAX_LEN * 2];
        s_encode_string_escaped(escaped_name, sizeof(escaped_name), report->name);
        n = snprintf(buf + pos, buf_len - pos, ",\"name\":\"%s\"", escaped_name);
    } else {
        n = snprintf(buf + pos, buf_len - pos, ",\"name\":null");
    }
    if (n < 0 || (uint16_t)n >= buf_len - pos) return -203;
    pos += (uint16_t)n;

    /* uuids */
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

    /* manufacturer data */
    if (report->has_manu) {
        n = snprintf(buf + pos, buf_len - pos, ",\"manu\":{\"id\":\"%04X\",\"data\":\"",
                     report->manu_id);
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

    /* closing brace */
    n = snprintf(buf + pos, buf_len - pos, "}");
    if (n < 0 || (uint16_t)n >= buf_len - pos) return -203;
    pos += (uint16_t)n;

    if (out_len != NULL) {
        *out_len = pos;
    }
    return 0;
}
