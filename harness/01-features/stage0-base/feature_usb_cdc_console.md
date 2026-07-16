# Feature: USB CDC Console

**Feature ID:** F0.1  
**Stage:** 0 (Base Infrastructure)  
**Layer:** HAL (Hardware Abstraction Layer)  
**Dependencies:** None  
**Status:** Defined  

## Overview

Initialize and manage USB CDC serial port for command/response communication. Buffer incoming data until newline, parse commands, and send JSON responses.

## Source Files

- **Interface:** `interfaces/usb_if.h`
- **Implementation:** `firmware/components/usb/usb_cdc_console.c`

## Test Files

- **Host tests:** `tests/host/test_usb_cdc.c` (host-testable parts)
- **Target tests:** `tests/harness/test_usb_on_target.c`

## Description

The USB CDC Console provides a serial command interface over USB. It initializes the USB CDC port, buffers incoming characters into lines (delimited by `\n`), and provides APIs to send and receive text lines. JSON responses are formatted and sent as complete lines.

## Public API

```c
int usb_console_init(void);
int usb_console_send_line(const char *line);
int usb_console_read_line(char *buf, uint16_t buf_len, uint32_t timeout_ms);
int usb_console_send_json(const char *json_line);
```

### API Details

- **usb_console_init()**: Initialize USB CDC port and internal buffers. Returns 0 on success, negative error code on failure.
- **usb_console_send_line(const char *line)**: Send a text line to USB. Appends `\n` automatically. Returns 0 on success, -501 if USB not connected, -502 if NULL parameter.
- **usb_console_read_line(char *buf, uint16_t buf_len, uint32_t timeout_ms)**: Read a complete line from USB buffer. Blocks until newline received or timeout. Returns line length on success, -503 on timeout, -504 if buffer overflow (line truncated), -502 if NULL parameter.
- **usb_console_send_json(const char *json_line)**: Send JSON string to USB. Appends `\n` automatically. Returns 0 on success, -501 if USB not connected, -502 if NULL parameter.

## Data Structures

```c
// Internal buffers (static allocation)
static char usb_rx_buffer[256];      // RX line buffer
static char usb_tx_buffer[512];      // TX JSON buffer
```

## Transport Protocol

- **Line ending:** `\n` (newline)
- **Ignore:** `\r` (carriage return)
- **Max line length:** 256 bytes (excluding `\n`)
- **Command case:** Case-sensitive
- **Buffer overflow:** Truncate line and return error code

## Error Codes

- **0:** Success
- **-501:** USB not connected
- **-502:** NULL parameter or invalid argument
- **-503:** Read timeout
- **-504:** Buffer overflow (line truncated)

## Acceptance Criteria

- [ ] `usb_console_init()` returns 0 on successful initialization
- [ ] `usb_console_send_line()` outputs text followed by `\n` to USB port
- [ ] `usb_console_read_line()` returns complete line when `\n` received
- [ ] Partial lines (no `\n`) cause blocking until timeout
- [ ] Buffer overflow (>256 bytes) truncates line and returns -504
- [ ] NULL parameters return -502
- [ ] Send operations return -501 when USB not connected

## Test Cases

### TC-1: Init success
- **Action:** Call `usb_console_init()`
- **Expected:** Returns 0, USB CDC port initialized

### TC-2: Send basic string
- **Action:** Call `usb_console_send_line("hello")`
- **Expected:** Returns 0, "hello\n" sent to USB

### TC-3: Read complete line
- **Action:** Send "test\n" to USB, call `usb_console_read_line(buf, 256, 1000)`
- **Expected:** Returns 4, buf contains "test"

### TC-4: Partial line arrival (no newline yet)
- **Action:** Send "partial" to USB (no `\n`), call `usb_console_read_line(buf, 256, 100)`
- **Expected:** Returns -503 (timeout), buf unchanged

### TC-5: Buffer overflow (>256 bytes)
- **Action:** Send 300 bytes without `\n`, call `usb_console_read_line(buf, 256, 1000)`
- **Expected:** Returns -504, buf contains first 255 chars (truncated)

### TC-6: NULL params
- **Action:** Call `usb_console_send_line(NULL)`
- **Expected:** Returns -502
- **Action:** Call `usb_console_read_line(NULL, 256, 1000)`
- **Expected:** Returns -502

### TC-7: Send when USB not connected
- **Action:** Call `usb_console_send_line("test")` with USB disconnected
- **Expected:** Returns -501

## Non-Functional Requirements

- **No dynamic allocation:** All buffers statically allocated
- **Thread safety:** TX operations protected by mutex
- **Reentrant:** Multiple tasks can call send functions safely
- **Performance:** No blocking on TX (non-blocking send)

## Constraints

- **Buffer sizes:** RX 256 bytes, TX 512 bytes (compile-time constants)
- **Line ending:** Only `\n` recognized as line delimiter
- **Command parsing:** Case-sensitive matching

## Integration Notes

- USB CDC driver must be initialized before calling this module
- This module does not parse commands, only buffers lines
- Command parsing is handled by higher-level modules
- JSON encoder (F1.2) uses `usb_console_send_json()` for output
