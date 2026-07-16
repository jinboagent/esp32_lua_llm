#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/*
 * BLE Advertisement Parser Interface
 *
 * Parses raw BLE advertising data (AD structures) into a structured format.
 * Pure computation module — no ESP-IDF dependencies, host-testable.
 *
 * Error codes (module range -100 to -199):
 *   0        Success
 *   -100     General failure
 *   -101     Invalid parameter (bad value, out of range)
 *   -102     NULL pointer
 *   -103     Length error (too short, truncated, or zero)
 *   -104     Malformed AD structure (length field exceeds remaining data)
 */

#define PROTO_ADV_DATA_MAX_LEN      31
#define PROTO_DEVICE_NAME_MAX_LEN   32
#define PROTO_UUID16_MAX_COUNT      10
#define PROTO_MANU_DATA_MAX_LEN     31
#define PROTO_ADDR_LEN              6

typedef enum {
    PROTO_ADDR_TYPE_PUBLIC = 0,
    PROTO_ADDR_TYPE_RANDOM = 1,
} proto_addr_type_t;

typedef struct {
    uint8_t  addr[PROTO_ADDR_LEN];
    proto_addr_type_t addr_type;
    int8_t   rssi;
    uint32_t ts_ms;

    char     name[PROTO_DEVICE_NAME_MAX_LEN];
    bool     has_name;

    uint16_t uuid16_list[PROTO_UUID16_MAX_COUNT];
    uint8_t  uuid16_count;

    uint16_t manu_id;
    bool     has_manu;
    uint8_t  manu_data[PROTO_MANU_DATA_MAX_LEN];
    uint8_t  manu_len;

    int8_t   tx_power;
    bool     has_tx_power;

    uint8_t  flags;
    bool     has_flags;
} proto_adv_report_t;

/*
 * Parse raw BLE advertising data into a structured report.
 *
 * @param raw_data  Raw AD structure bytes (from BLE scan callback)
 * @param raw_len   Length of raw_data (max 31 for legacy adv, max 255 for extended)
 * @param[out] out  Parsed report. Caller-owned buffer.
 * @return 0 on success, negative error code on failure.
 *         On error, out is not modified.
 */
int proto_parse_adv_data(const uint8_t *raw_data, uint16_t raw_len,
                         proto_adv_report_t *out);

/*
 * Initialize a report struct to default/empty values.
 *
 * @param[out] report  Report to initialize. Must not be NULL.
 * @return 0 on success, -102 if report is NULL.
 */
int proto_report_init(proto_adv_report_t *report);
