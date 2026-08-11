#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "proto_if.h"

#ifdef __cplusplus
extern "C" {
#endif

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

/*
 * Escape a string for safe embedding inside a JSON string literal.
 *
 * Escapes '"' -> \" , '\\' -> \\ , and control chars (< 0x20) as
 * \b \f \n \r \t or \u00XX. Output is always NUL-terminated. If the
 * escaped result does not fit, it is truncated at a safe boundary so
 * the output remains VALID JSON (escape sequences are never split).
 *
 * Needed everywhere dynamic text (Lua error messages, script results,
 * user-supplied filter patterns) is interpolated into responses
 * (H1 fix, bug_check 2026-08-11).
 *
 * @param in       Input string (null-terminated). Must not be NULL.
 * @param[out] out Output buffer. Must not be NULL.
 * @param out_len  Size of output buffer (>= 2).
 * @return 0 on success, -202 on NULL parameter, -203 if out_len < 2.
 */
int json_escape_str(const char *in, char *out, uint16_t out_len);

#ifdef __cplusplus
}
#endif
