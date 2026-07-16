#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "proto_if.h"

/*
 * JSON Lines Encoder Interface
 *
 * Encodes a proto_adv_report_t into a single JSON line string.
 * Pure computation module — no ESP-IDF dependencies, host-testable.
 *
 * Error codes (module range -200 to -299):
 *   0        Success
 *   -200     General failure
 *   -201     Invalid parameter
 *   -202     NULL pointer
 *   -203     Buffer too small (output truncated)
 */

#define JSON_LINE_MAX_LEN   512

/*
 * Encode an advertisement report as a JSON line.
 * Output does NOT include trailing newline — caller appends '\n'.
 *
 * @param report    Parsed advertisement report (read-only)
 * @param[out] buf  Output buffer. Caller-owned.
 * @param buf_len   Size of output buffer in bytes.
 * @param[out] out_len  Number of bytes written (excluding null terminator).
 *                     May be NULL if caller doesn't need the length.
 * @return 0 on success, negative error code on failure.
 */
int json_encode_adv(const proto_adv_report_t *report,
                    char *buf, uint16_t buf_len,
                    uint16_t *out_len);
