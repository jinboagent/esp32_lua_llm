#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * USB CDC Console Interface
 *
 * Provides line-buffered serial I/O over USB CDC.
 * Buffers incoming characters until newline, provides send/receive APIs.
 *
 * Error codes (module range -500 to -599):
 *   0        Success
 *   -500     General failure
 *   -501     USB not connected / not initialized
 *   -502     NULL parameter or invalid argument
 *   -503     Read timeout
 *   -504     Buffer overflow (line truncated)
 */

#define USB_RX_BUFFER_SIZE  256
#define USB_TX_BUFFER_SIZE  512

/*
 * Initialize USB CDC console.
 * Sets up internal buffers and USB serial driver.
 *
 * @return 0 on success, negative error code on failure.
 */
int usb_console_init(void);

/*
 * Send a text line over USB CDC.
 * Appends '\n' automatically.
 *
 * @param line  Null-terminated string to send. Must not be NULL.
 * @return 0 on success, -502 if NULL, -501 if USB not ready.
 */
int usb_console_send_line(const char *line);

/*
 * Read a complete line from USB CDC buffer.
 * Blocks until '\n' received or timeout expires.
 *
 * @param buf         Output buffer for the line (without '\n').
 * @param buf_len     Size of output buffer.
 * @param timeout_ms  Max wait time in milliseconds. 0 = non-blocking.
 * @return Line length on success (>=0), negative error code on failure.
 *         -502 if NULL param, -503 on timeout, -504 if truncated.
 */
int usb_console_read_line(char *buf, uint16_t buf_len, uint32_t timeout_ms);

/*
 * Send a JSON string over USB CDC.
 * Appends '\n' automatically. Same as usb_console_send_line but
 * semantically distinct for JSON output.
 *
 * @param json_line  Null-terminated JSON string. Must not be NULL.
 * @return 0 on success, -502 if NULL, -501 if USB not ready.
 */
int usb_console_send_json(const char *json_line);

/*
 * Non-blocking one-byte lookahead for Ctrl+C (F2.4 improvement pass
 * 2026-08-22): consumes and reports a pending 0x03; any other byte is
 * pushed back untouched so the normal line stream is preserved. Lets a
 * bounded blocking wait (conn direct start) stay Ctrl+C-responsive
 * without a separate RX task.
 *
 * @return true if a 0x03 was consumed, false otherwise (no byte, or a
 *         non-interrupt byte pushed back).
 */
bool usb_console_poll_interrupt(void);

#ifdef __cplusplus
}
#endif
