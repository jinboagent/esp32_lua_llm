# Feature: JSON Lines Encoder

**Feature ID:** F1.2  
**Stage:** 1 (Protocol Layer)  
**Layer:** Protocol  
**Dependencies:** F1.1 (adv_parser)  
**Status:** Defined  

## Overview

Encode parsed BLE advertisement reports as JSON Lines format. Output single-line JSON strings suitable for USB CDC transmission.

## Source Files

- **Interface:** `interfaces/json_if.h`
- **Implementation:** `firmware/components/json_enc/json_encoder.c`

## Test Files

- **Host tests:** `tests/host/test_json_encoder.c`

## Description

The JSON Lines Encoder converts `proto_adv_report_t` structures into JSON-formatted strings. Each report becomes a single JSON line (no trailing newline). The output is designed for consumption by the USB CDC console module.

## Public API

```c
int json_encode_adv(const proto_adv_report_t *report, char *buf, uint16_t buf_len, uint16_t *out_len);
```

### API Details

- **json_encode_adv(const proto_adv_report_t *report, char *buf, uint16_t buf_len, uint16_t *out_len)**: Encode report as JSON line. Returns 0 on success, -202 if NULL pointer, -203 if buffer too small. Output length written to out_len.

## Data Structures

```c
// Input (from F1.1)
typedef struct {
    uint8_t addr[6];
    uint8_t addr_type;
    int8_t rssi;
    uint32_t ts_ms;
    char name[32];
    bool has_name;
    uint16_t uuid16_list[10];
    uint8_t uuid16_count;
    uint16_t manu_id;
    bool has_manu;
    uint8_t manu_data[31];
    uint8_t manu_len;
    int8_t tx_power;
    bool has_tx_power;
    uint8_t flags;
    bool has_flags;
} proto_adv_report_t;

// Buffer size
#define JSON_LINE_MAX_LEN 512
```

## Output Format

```json
{
  "ts": 1234567890,
  "addr": "AA:BB:CC:DD:EE:FF",
  "type": "public|random",
  "rssi": -45,
  "name": "DeviceName" | null,
  "uuids": ["180A", "180F", ...],
  "manu": {
    "id": "004C",
    "data": "0215..."
  } | null
}
```

### Field Specifications

- **ts:** Timestamp in milliseconds (uint32_t)
- **addr:** MAC address as colon-separated hex string (uppercase)
- **type:** "public" if addr_type=0, "random" if addr_type=1
- **rssi:** Signal strength in dBm (int8_t)
- **name:** Device name string or null if has_name=false
- **uuids:** Array of 4-character hex strings (big-endian, e.g., "180A")
- **manu:** Object with id (4-char hex) and data (hex string) or null if has_manu=false

## String Escaping

The following characters must be escaped in JSON strings:

- `"` → `\"`
- `\` → `\\`
- `\n` → `\n`
- `\r` → `\r`
- `\t` → `\t`
- Control chars (0x00-0x1F) → `\uXXXX`

## Error Codes

- **0:** Success
- **-202:** NULL pointer (report, buf, or out_len is NULL)
- **-203:** Buffer too small (output would exceed buf_len)

## Constraints

- **No ESP-IDF headers:** Only standard C headers allowed
- **Host-testable:** Pure C implementation
- **Buffer size:** 512 bytes max (JSON_LINE_MAX_LEN)
- **UUID format:** Big-endian hex (e.g., UUID 0x0A18 → "180A")
- **No trailing newline:** Caller (USB CDC) appends `\n`
- **Single line:** No pretty-printing, no newlines in output

## Acceptance Criteria

- [ ] Basic report encodes to valid JSON
- [ ] Null name produces `"name":null`
- [ ] Manufacturer data formatted as object with id and data
- [ ] Multiple UUIDs encoded as array
- [ ] Empty UUID array encoded as `[]`
- [ ] String escaping works for special characters
- [ ] Random address type encoded correctly
- [ ] Output is valid JSON structure
- [ ] Buffer too small returns error

## Test Cases

### TC-1: Basic report
- **Action:** Encode report with name, 1 UUID, public addr
- **Expected:** Returns 0, valid JSON with all fields

### TC-2: No name (null)
- **Action:** Encode report with has_name=false
- **Expected:** Returns 0, `"name":null` in output

### TC-3: With manufacturer
- **Action:** Encode report with has_manu=true
- **Expected:** Returns 0, `"manu":{"id":"XXXX","data":"..."}` in output

### TC-4: Multiple UUIDs
- **Action:** Encode report with 3 UUIDs
- **Expected:** Returns 0, `"uuids":["XXXX","YYYY","ZZZZ"]` in output

### TC-5: Empty UUIDs
- **Action:** Encode report with uuid16_count=0
- **Expected:** Returns 0, `"uuids":[]` in output

### TC-6: NULL report
- **Action:** Call `json_encode_adv(NULL, buf, len, out_len)`
- **Expected:** Returns -202

### TC-7: NULL buffer
- **Action:** Call `json_encode_adv(report, NULL, len, out_len)`
- **Expected:** Returns -202

### TC-8: Buffer too small
- **Action:** Encode large report with 10-byte buffer
- **Expected:** Returns -203

### TC-9: Name escaping
- **Action:** Encode report with name containing `"`, `\`, control chars
- **Expected:** Returns 0, special chars properly escaped

### TC-10: Random addr
- **Action:** Encode report with addr_type=1
- **Expected:** Returns 0, `"type":"random"` in output

### TC-11: Valid JSON structure
- **Action:** Parse output with JSON parser
- **Expected:** Valid JSON, all fields present and correct type

## Non-Functional Requirements

- **No dynamic allocation:** All operations use provided buffer
- **Pure C:** No C++ features, no ESP-IDF headers
- **Host-testable:** Compiles and runs on host PC
- **Performance:** Encode in <50 microseconds (target)
- **Deterministic:** Same input always produces same output

## Integration Notes

- Consumes `proto_adv_report_t` from F1.1 (adv_parser)
- Output consumed by F0.1 (USB CDC console) via `usb_console_send_json()`
- Filter engine (F1.3) operates before encoding, not after
- JSON output is single-line for line-based protocols
- Timestamp (ts) comes from BLE scanner, not generated here
