#include "filter_if.h"
#include <string.h>
#include <ctype.h>

#ifdef HOST_BUILD
/* Host unit tests are single-threaded */
void filter_lock(void)   { }
void filter_unlock(void) { }
#else
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
static SemaphoreHandle_t s_filter_mutex = NULL;

void filter_lock(void)
{
    if (s_filter_mutex != NULL) {
        xSemaphoreTake(s_filter_mutex, portMAX_DELAY);
    }
}

void filter_unlock(void)
{
    if (s_filter_mutex != NULL) {
        xSemaphoreGive(s_filter_mutex);
    }
}
#endif

int filter_init(filter_engine_t *eng)
{
    if (eng == NULL) {
        return -302;
    }
    memset(eng, 0, sizeof(filter_engine_t));
#ifndef HOST_BUILD
    if (s_filter_mutex == NULL) {
        /* created once at boot, before the pipeline task exists */
        s_filter_mutex = xSemaphoreCreateMutex();
    }
#endif
    return 0;
}

int filter_clear(filter_engine_t *eng)
{
    if (eng == NULL) {
        return -302;
    }
    memset(eng->rules, 0, sizeof(eng->rules));
    eng->rule_count = 0;
    return 0;
}

int filter_add_rule(filter_engine_t *eng, filter_type_t type,
                    const char *pattern, int8_t rssi_val)
{
    if (eng == NULL) {
        return -302;
    }
    if (type >= FILTER_TYPE_COUNT) {
        return -301;
    }
    if (type != FILTER_TYPE_RSSI && pattern == NULL) {
        return -302;
    }
    if (eng->rule_count >= FILTER_MAX_RULES) {
        return -303;
    }

    filter_rule_t *rule = &eng->rules[eng->rule_count];
    rule->type = type;
    rule->rssi_threshold = rssi_val;

    if (pattern != NULL) {
        size_t plen = strlen(pattern);
        if (plen >= FILTER_PATTERN_MAX_LEN) {
            plen = FILTER_PATTERN_MAX_LEN - 1;
        }
        memcpy(rule->pattern, pattern, plen);
        rule->pattern[plen] = '\0';
    } else {
        rule->pattern[0] = '\0';
    }

    eng->rule_count++;
    return 0;
}

static bool s_wildcard_match(const char *pattern, const char *str)
{
    const char *star_p = NULL;
    const char *star_s = NULL;

    while (*str != '\0') {
        if (*pattern == '*' ) {
            star_p = pattern++;
            star_s = str;
        } else if (tolower((unsigned char)*pattern) == tolower((unsigned char)*str)) {
            pattern++;
            str++;
        } else if (star_p != NULL) {
            pattern = star_p + 1;
            str = ++star_s;
        } else {
            return false;
        }
    }
    while (*pattern == '*') {
        pattern++;
    }
    return *pattern == '\0';
}

static bool s_match_name(const filter_rule_t *rule, const proto_adv_report_t *report)
{
    if (!report->has_name) {
        return false;
    }
    return s_wildcard_match(rule->pattern, report->name);
}

static bool s_match_uuid(const filter_rule_t *rule, const proto_adv_report_t *report)
{
    unsigned int target_uuid = 0;
    if (strlen(rule->pattern) != 4) {
        return false;
    }
    for (int i = 0; i < 4; i++) {
        char c = rule->pattern[i];
        unsigned int digit;
        if (c >= '0' && c <= '9') {
            digit = (unsigned int)(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            digit = (unsigned int)(c - 'a' + 10);
        } else if (c >= 'A' && c <= 'F') {
            digit = (unsigned int)(c - 'A' + 10);
        } else {
            return false;
        }
        target_uuid = (target_uuid << 4) | digit;
    }

    for (uint8_t i = 0; i < report->uuid16_count; i++) {
        if (report->uuid16_list[i] == (uint16_t)target_uuid) {
            return true;
        }
    }
    return false;
}

static bool s_match_rssi(const filter_rule_t *rule, const proto_adv_report_t *report)
{
    return report->rssi >= rule->rssi_threshold;
}

static bool s_match_mac(const filter_rule_t *rule, const proto_adv_report_t *report)
{
    if (strlen(rule->pattern) != 17) {
        return false;
    }
    uint8_t target[6];
    const char *p = rule->pattern;
    for (int i = 0; i < 6; i++) {
        if (i > 0) {
            if (*p != ':') return false;
            p++;
        }
        unsigned int byte_val = 0;
        for (int j = 0; j < 2; j++) {
            char c = *p++;
            unsigned int digit;
            if (c >= '0' && c <= '9') {
                digit = (unsigned int)(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                digit = (unsigned int)(c - 'a' + 10);
            } else if (c >= 'A' && c <= 'F') {
                digit = (unsigned int)(c - 'A' + 10);
            } else {
                return false;
            }
            byte_val = (byte_val << 4) | digit;
        }
        target[i] = (uint8_t)byte_val;
    }

    return memcmp(target, report->addr, 6) == 0;
}

static bool s_evaluate_type(const filter_engine_t *eng, filter_type_t type,
                            const proto_adv_report_t *report)
{
    bool has_type_filter = false;
    for (uint8_t i = 0; i < eng->rule_count; i++) {
        if (eng->rules[i].type != type) {
            continue;
        }
        has_type_filter = true;
        bool matched = false;
        switch (type) {
        case FILTER_TYPE_NAME:
            matched = s_match_name(&eng->rules[i], report);
            break;
        case FILTER_TYPE_UUID:
            matched = s_match_uuid(&eng->rules[i], report);
            break;
        case FILTER_TYPE_RSSI:
            matched = s_match_rssi(&eng->rules[i], report);
            break;
        case FILTER_TYPE_MAC:
            matched = s_match_mac(&eng->rules[i], report);
            break;
        default:
            break;
        }
        if (matched) {
            return true;
        }
    }
    return !has_type_filter;
}

bool filter_evaluate(const filter_engine_t *eng,
                     const proto_adv_report_t *report)
{
    if (eng == NULL || report == NULL) {
        return false;
    }
    if (eng->rule_count == 0) {
        return true;
    }

    for (int t = 0; t < (int)FILTER_TYPE_COUNT; t++) {
        if (!s_evaluate_type(eng, (filter_type_t)t, report)) {
            return false;
        }
    }
    return true;
}

int filter_get_count(const filter_engine_t *eng)
{
    if (eng == NULL) {
        return -1;
    }
    return (int)eng->rule_count;
}
