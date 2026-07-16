# Feature: BLE Advertisement Parser

**Feature ID:** F1.1  
**Stage:** 1 (Protocol Layer)  
**Layer:** Protocol  
**Dependencies:** None  
**Status:** Defined  

## Overview

Parse raw BLE advertisement data structures into structured reports. Extract device name, UUID16 list, manufacturer data, TX power, and flags from raw AD bytes.

## Source Files

- **Interface:** `interfaces/proto_if.h`
- **Implementation:** `firmware/components/proto/proto_adv_parse.c`

## Test Files

- **Host tests:** `tests/host/test_adv_parser.c`

## Description

The BLE Advertisement Parser converts raw BLE AD (Advertisement Data) structures into a structured `proto_adv_report_t` format. It parses standard BLE advertisement fields including complete/shortened local name, UUID16 lists, manufacturer specific data, TX power level, and flags.

## Public API

```c
int proto_parse_adv_data(const uint8_t *raw_data, uint16_t raw_len, proto_adv_report_t *out);
void proto_report_init(proto_adv_report_t *report);
```

### API Details

- **proto_parse_adv_data(const uint8_t *raw_data, uint16_t raw_len, proto_adv_report_t *out)**: Parse raw AD data into structured report. Returns 0 on success, -102 if NULL pointer, -103 if zero length, -104 if malformed AD structure.
- **proto_report_init(proto_adv_report_t *report)**: Initialize report structure to default values. Sets all has_* flags to false, counts to 0.

## Data Structures

```c
typedef struct {
    // Device address
    uint8_t addr[6];
    uint8_t addr_type;  // 0=public, 1=random
    
    // Signal strength
    int8_t rssi;
    uint32_t ts_ms;  // timestamp
    
    // Device name
    char name[32];
    bool has_name;
    
    // UUID16 list
    uint16_t uuid16_list[10];
    uint8_t uuid16_count;
    
    // Manufacturer data
    uint16_t manu_id;
    bool has_manu;
    uint8_t manu_data[31];
    uint8_t manu_len;
    
    // TX power
    int8_t tx_power;
    bool has_tx_power;
    
    // Flags
    uint8_t flags;
    bool has_flags;
} proto_adv_report_t;
```

## AD Structure Format

```
[Length] [Type] [Data...]
```

- Length: 1 byte (length of type + data)
- Type: 1 byte (AD type)
- Data: 0-31 bytes

### Supported AD Types

- **0x01:** Flags
- **0x02/0x03:** Incomplete/Complete List of 16-bit Service UUIDs
- **0x08/0x09:** Shortened/Complete Local Name
- **0x0A:** TX Power Level
- **0xFF:** Manufacturer Specific Data

## Error Codes

- **0:** Success
- **-102:** NULL pointer (raw_data or out is NULL)
- **-103:** Zero length (raw_len == 0)
- **-104:** Malformed AD (length field exceeds remaining bytes)

## Constraints

- **No ESP-IDF headers:** Only stdint.h, stdbool.h, stddef.h, string.h allowed
- **Host-testable:** Pure C implementation, no ESP-IDF dependencies
- **Max name length:** 29 characters (31 bytes - 2 for length/type)
- **Max UUID16 count:** 10 (array size limit)
- **Max manufacturer data:** 31 bytes (AD structure limit)
- **Duplicate UUIDs:** Preserved (not deduplicated)

## Acceptance Criteria

- [ ] Parse valid advertisement with name + UUIDs + flags
- [ ] Parse manufacturer specific data correctly
- [ ] Parse TX power level
- [ ] Handle malformed AD structure (length exceeds remaining)
- [ ] Handle empty payload (raw_len = 0)
- [ ] Handle max-length name (29 chars)
- [ ] Preserve duplicate UUIDs
- [ ] Initialize report structure correctly
- [ ] Return appropriate error codes for invalid inputs

## Test Cases

### TC-1: Valid complete adv
- **Action:** Parse AD with name + 2 UUIDs + flags
- **Expected:** Returns 0, all fields populated correctly

### TC-2: Malformed length
- **Action:** Parse AD with length field > remaining bytes
- **Expected:** Returns -104

### TC-3: Empty payload
- **Action:** Parse with raw_len = 0
- **Expected:** Returns -103

### TC-4: Duplicate UUIDs
- **Action:** Parse AD with UUID list containing duplicates
- **Expected:** Returns 0, all UUIDs preserved in order

### TC-5: NULL raw
- **Action:** Call `proto_parse_adv_data(NULL, len, out)`
- **Expected:** Returns -102

### TC-6: NULL output
- **Action:** Call `proto_parse_adv_data(raw, len, NULL)`
- **Expected:** Returns -102

### TC-7: Zero length
- **Action:** Call `proto_parse_adv_data(raw, 0, out)`
- **Expected:** Returns -103

### TC-8: Manufacturer data
- **Action:** Parse AD with manufacturer ID + data
- **Expected:** Returns 0, manu_id and manu_data populated

### TC-9: TX power
- **Action:** Parse AD with TX power level
- **Expected:** Returns 0, tx_power and has_tx_power set

### TC-10: Max-length name
- **Action:** Parse AD with 29-character name
- **Expected:** Returns 0, name field contains full string

### TC-11: Report init
- **Action:** Call `proto_report_init(&report)`
- **Expected:** All has_* flags false, counts 0

### TC-12: Report init NULL
- **Action:** Call `proto_report_init(NULL)`
- **Expected:** No crash (undefined behavior, but should not crash)

### TC-13: Complex multi-field
- **Action:** Parse AD with name + UUIDs + manu + TX power + flags
- **Expected:** Returns 0, all fields parsed correctly

### TC-14: Shortened name
- **Action:** Parse AD with shortened local name (type 0x08)
- **Expected:** Returns 0, name field populated

## Non-Functional Requirements

- **No dynamic allocation:** All structures statically sized
- **Pure C:** No C++ features, no ESP-IDF headers
- **Host-testable:** Compiles and runs on host PC without ESP-IDF
- **Performance:** Parse in <100 microseconds (target)

## Integration Notes

- This module does not receive BLE data, only parses it
- BLE scanner (F2.x) calls this module with raw AD data
- JSON encoder (F1.2) consumes `proto_adv_report_t` output
- Filter engine (F1.3) evaluates `proto_adv_report_t` against rules
- Address and RSSI fields populated by caller, not from AD data
