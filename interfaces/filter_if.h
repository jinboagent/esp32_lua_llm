#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "proto_if.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * BLE Advertisement Filter Engine Interface
 *
 * Rule-based filtering of parsed BLE advertisements.
 *
 * Combination logic:
 *   - Same type filters: OR (match any one → pass)
 *   - Different type filters: AND (must pass all active types)
 *   - If no filters active: all advertisements pass
 *
 * Error codes (module range -300 to -399):
 *   0        Success
 *   -300     General failure
 *   -301     Invalid parameter
 *   -302     NULL pointer
 *   -303     Filter list full (max FILTER_MAX_RULES reached)
 *   -304     Filter not found
 *   -305     Invalid pattern (e.g., wildcard syntax error)
 */

#define FILTER_MAX_RULES          16
#define FILTER_PATTERN_MAX_LEN    32

typedef enum {
    FILTER_TYPE_NAME = 0,
    FILTER_TYPE_UUID,
    FILTER_TYPE_RSSI,
    FILTER_TYPE_MAC,
    FILTER_TYPE_COUNT
} filter_type_t;

typedef struct {
    filter_type_t type;
    char     pattern[FILTER_PATTERN_MAX_LEN];
    int8_t   rssi_threshold;
} filter_rule_t;

typedef struct {
    filter_rule_t rules[FILTER_MAX_RULES];
    uint8_t       rule_count;
} filter_engine_t;

/*
 * Initialize the filter engine (clears all rules).
 *
 * @param[out] eng  Filter engine to initialize. Must not be NULL.
 * @return 0 on success, -302 if eng is NULL.
 */
int filter_init(filter_engine_t *eng);

/*
 * Add a filter rule.
 *
 * For FILTER_TYPE_NAME: pattern supports '*' wildcard (e.g., "Sensor*")
 * For FILTER_TYPE_UUID: pattern is 4-char hex string (e.g., "180A")
 * For FILTER_TYPE_RSSI: pattern is ignored; rssi_threshold is used
 * For FILTER_TYPE_MAC: pattern is "XX:XX:XX:XX:XX:XX" format
 *
 * @param eng       Filter engine. Must be initialized.
 * @param type      Filter type.
 * @param pattern   Pattern string (null-terminated). NULL for RSSI type.
 * @param rssi_val  RSSI threshold (only used for FILTER_TYPE_RSSI).
 * @return 0 on success, negative error code on failure.
 */
int filter_add_rule(filter_engine_t *eng, filter_type_t type,
                    const char *pattern, int8_t rssi_val);

/*
 * Remove all filter rules.
 *
 * @param eng  Filter engine. Must not be NULL.
 * @return 0 on success, -302 if eng is NULL.
 */
int filter_clear(filter_engine_t *eng);

/*
 * Evaluate whether an advertisement passes all active filters.
 *
 * @param eng     Filter engine with active rules.
 * @param report  Advertisement report to evaluate (read-only).
 * @return true if report passes all filters (or no filters active),
 *         false if report should be suppressed.
 */
bool filter_evaluate(const filter_engine_t *eng,
                     const proto_adv_report_t *report);

/*
 * Get the number of active filter rules.
 *
 * @param eng  Filter engine. Must not be NULL.
 * @return Number of active rules, or -1 if eng is NULL.
 */
int filter_get_count(const filter_engine_t *eng);

#ifdef __cplusplus
}
#endif
