# Feature: BLE Advertisement Filter Engine

**Feature ID:** F1.3  
**Stage:** 1 (Protocol Layer)  
**Layer:** Protocol  
**Dependencies:** F1.1 (adv_parser)  
**Status:** Defined  

## Overview

Rule-based filtering of parsed BLE advertisements. Supports filtering by device name (with wildcards), UUID16, RSSI threshold, and MAC address. Combines multiple rules with AND/OR logic.

## Source Files

- **Interface:** `interfaces/filter_if.h`
- **Implementation:** `firmware/components/filter/filter_engine.c`

## Test Files

- **Host tests:** `tests/host/test_filter_engine.c`

## Description

The Filter Engine evaluates BLE advertisement reports against a set of user-defined rules. Rules can match on device name (with wildcard support), UUID16 presence, RSSI threshold, or MAC address. Multiple rules of the same type are combined with OR logic (match any). Rules of different types are combined with AND logic (must pass all).

## Public API

```c
int filter_init(filter_engine_t *eng);
int filter_add_rule(filter_engine_t *eng, filter_type_t type, const char *pattern, int8_t rssi_val);
int filter_clear(filter_engine_t *eng);
bool filter_evaluate(const filter_engine_t *eng, const proto_adv_report_t *report);
int filter_get_count(const filter_engine_t *eng);
```

### API Details

- **filter_init(filter_engine_t *eng)**: Initialize filter engine. Returns 0 on success, -302 if NULL pointer.
- **filter_add_rule(filter_engine_t *eng, filter_type_t type, const char *pattern, int8_t rssi_val)**: Add filter rule. Returns 0 on success, -302 if NULL pointer, -301 if invalid parameter, -303 if filter list full.
- **filter_clear(filter_engine_t *eng)**: Remove all rules. Returns 0 on success, -302 if NULL pointer.
- **filter_evaluate(const filter_engine_t *eng, const proto_adv_report_t *report)**: Evaluate report against rules. Returns true if report passes all filters, false if filtered out. Returns true if no rules defined.
- **filter_get_count(const filter_engine_t *eng)**: Get number of active rules. Returns count (0-16), -302 if NULL pointer.

## Data Structures

```c
typedef enum {
    FILTER_TYPE_NAME,   // Match device name (wildcard supported)
    FILTER_TYPE_UUID,   // Match UUID16 presence
    FILTER_TYPE_RSSI,   // Match RSSI threshold
    FILTER_TYPE_MAC     // Match MAC address
} filter_type_t;

typedef struct {
    filter_type_t type;
    char pattern[32];   // Name pattern, UUID hex, or MAC address
    int8_t rssi_val;    // RSSI threshold (for FILTER_TYPE_RSSI)
} filter_rule_t;

typedef struct {
    filter_rule_t rules[16];
    uint8_t rule_count;
} filter_engine_t;

#define FILTER_MAX_RULES 16
#define FILTER_PATTERN_MAX_LEN 32
```

## Filter Logic

### Combination Rules

- **Same type = OR:** Multiple rules of the same type are OR'd together (match any)
- **Different types = AND:** Rules of different types are AND'd together (must pass all)
- **No rules = pass all:** Empty filter list passes all advertisements

### Matching Rules

#### Name Matching (FILTER_TYPE_NAME)
- Case-insensitive comparison
- Supports `*` wildcard (matches any sequence)
- Examples:
  - `"Device"` matches "Device", "device", "DEVICE"
  - `"Dev*"` matches "Device", "Development", "dev123"
  - `"*Test*"` matches "MyTestDevice", "test", "TESTING"
  - `"*"` matches any name

#### UUID Matching (FILTER_TYPE_UUID)
- 4-character hex string comparison (case-insensitive)
- Matches if report contains the specified UUID16
- Example: `"180A"` matches report with UUID 0x180A

#### RSSI Matching (FILTER_TYPE_RSSI)
- Report RSSI must be >= threshold
- Example: threshold -60 matches RSSI -50, -60 but not -70

#### MAC Matching (FILTER_TYPE_MAC)
- Exact match on MAC address in format `XX:XX:XX:XX:XX:XX`
- Case-insensitive hex comparison
- Example: `"AA:BB:CC:DD:EE:FF"`

## Error Codes

- **0:** Success
- **-301:** Invalid parameter (bad pattern, invalid type, etc.)
- **-302:** NULL pointer
- **-303:** Filter list full (16 rules max)

## Constraints

- **Max rules:** 16 (FILTER_MAX_RULES)
- **Pattern max length:** 32 characters
- **No dynamic allocation:** All structures statically sized
- **Host-testable:** Pure C implementation
- **Case insensitive:** Name, UUID, and MAC matching are case-insensitive

## Acceptance Criteria

- [ ] No rules passes all advertisements
- [ ] Exact name match works
- [ ] Wildcard name match works
- [ ] Name matching is case-insensitive
- [ ] UUID match works
- [ ] RSSI threshold works (>=)
- [ ] MAC address match works
- [ ] Same-type rules combined with OR logic
- [ ] Different-type rules combined with AND logic
- [ ] Clear resets all rules
- [ ] List full returns error
- [ ] NULL parameters return error
- [ ] Name filter on nameless report returns false
- [ ] Wildcard `*` matches all names

## Test Cases

### TC-1: No rules passes all
- **Action:** Evaluate report with empty filter
- **Expected:** Returns true

### TC-2: Name exact match
- **Action:** Add name rule "Device", evaluate report with name "Device"
- **Expected:** Returns true

### TC-3: Name wildcard
- **Action:** Add name rule "Dev*", evaluate report with name "Development"
- **Expected:** Returns true

### TC-4: Case insensitive
- **Action:** Add name rule "device", evaluate report with name "DEVICE"
- **Expected:** Returns true

### TC-5: UUID match
- **Action:** Add UUID rule "180A", evaluate report with UUID 0x180A
- **Expected:** Returns true

### TC-6: RSSI threshold
- **Action:** Add RSSI rule -60, evaluate report with RSSI -50
- **Expected:** Returns true
- **Action:** Evaluate report with RSSI -70
- **Expected:** Returns false

### TC-7: MAC match
- **Action:** Add MAC rule "AA:BB:CC:DD:EE:FF", evaluate matching report
- **Expected:** Returns true

### TC-8: Same-type OR
- **Action:** Add 2 name rules "Dev*" and "Test*", evaluate report with name "Testing"
- **Expected:** Returns true (matches second rule)

### TC-9: Different-type AND
- **Action:** Add name rule "Dev*" and RSSI rule -60, evaluate report with name "Device" and RSSI -50
- **Expected:** Returns true (passes both)
- **Action:** Evaluate report with name "Device" and RSSI -70
- **Expected:** Returns false (fails RSSI)

### TC-10: Clear
- **Action:** Add 3 rules, call filter_clear, get count
- **Expected:** Returns 0

### TC-11: List full
- **Action:** Add 16 rules, attempt to add 17th
- **Expected:** Returns -303

### TC-12: NULL engine
- **Action:** Call filter_init(NULL)
- **Expected:** Returns -302

### TC-13: Name filter on nameless report
- **Action:** Add name rule "Device", evaluate report with has_name=false
- **Expected:** Returns false

### TC-14: Wildcard * matches all
- **Action:** Add name rule "*", evaluate report with any name
- **Expected:** Returns true

## Non-Functional Requirements

- **No dynamic allocation:** All structures statically sized
- **Pure C:** No C++ features, no ESP-IDF headers
- **Host-testable:** Compiles and runs on host PC
- **Performance:** Evaluate in <10 microseconds per rule (target)

## Integration Notes

- Consumes `proto_adv_report_t` from F1.1 (adv_parser)
- Filtering happens before JSON encoding (F1.2)
- Rules configured via command interface (F4.x)
- Filter state not persisted across reboots
- Empty pattern for name filter matches nothing (except wildcard `*`)
