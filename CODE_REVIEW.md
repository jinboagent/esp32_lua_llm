# Code Review: ESP32 BLE Sniffer Dongle

Review of Stage 1 pure-C modules (adv_parser, json_encoder, filter_engine),
tests, and ESP-IDF integration (main.c).

---

## Module 1: ADV Parser (`proto_adv_parse.c`)

### What's Good
- Clean separation: pure C, no ESP-IDF deps, host-testable
- Uses temp buffer then copies to output — on error, `out` is untouched (atomic semantics)
- Handles all common AD types: flags, UUID16, name (complete + shortened), TX power, manufacturer data
- First-occurrence wins for name/manufacturer (correct BLE behavior)
- UUIDs accumulate across multiple AD structures (also correct)

### Issues

**⚠️ No max length validation**

`PROTO_ADV_DATA_MAX_LEN` (31) is defined in the header but never used in the implementation.
The parser accepts `raw_len` up to 65535 (uint16_t). For legacy BLE, max is 31 bytes.
For extended advertising, up to 255. Should validate:

```c
if (raw_len > 255) {
    return -103;
}
```

**⚠️ `proto_adv_report_t` is ~120 bytes on the stack**

When the pipeline runs in a FreeRTOS task with 4KB stack, each layer
(parse → filter → encode) adds ~120 bytes. Worth documenting the cumulative stack impact.

**⚠️ Odd-length UUID data silently drops last byte**

```c
uint8_t count = len / 2;  // 3 bytes → 1 UUID, last byte silently dropped
```

A malformed AD with odd-length UUID data should probably be flagged rather
than silently truncated.

---

## Module 2: JSON Encoder (`json_encoder.c`)

### What's Good
- Every `snprintf` call is checked for overflow
- String escaping handles `"`, `\`, `\n`, `\r`, `\t`, and control chars
- Null fields produce proper JSON null (not empty string)
- Output is valid JSON — starts with `{`, ends with `}`, verified by test

### Issues

**🐛 `s_encode_mac` failure is silently ignored**

```c
char mac_str[18];
s_encode_mac(mac_str, sizeof(mac_str), report->addr);
// Return value not checked!
n = snprintf(buf + pos, buf_len - pos, ",\"addr\":\"%s\"", mac_str);
```

If `s_encode_mac` fails, `mac_str` contains uninitialized data.
In practice this can't happen (buffer is always exactly the right size),
but the return value should be checked for code quality.

**⚠️ Missing fields in JSON output**

The parser extracts `tx_power`, `flags`, and `has_flags`, but the JSON
encoder doesn't output them. Data is lost between parse and output.

Either add them to JSON output, or document that they're intentionally omitted.

**⚠️ Stack usage**

```c
char escaped_name[PROTO_DEVICE_NAME_MAX_LEN * 2];  // 64 bytes
char mac_str[18];                                    // 18 bytes
```

Total ~150 bytes locals. Fine for now, but track as pipeline layers are added.

---

## Module 3: Filter Engine (`filter_engine.c`)

### What's Good
- AND between types, OR within type — correctly implemented
- Case-insensitive name matching
- Wildcard `*` support works correctly
- `filter_evaluate` with NULL params returns `true` (fail-open = safe default)
- All match functions are static and well-isolated

### Issues

**🐛 `s_match_mac` doesn't validate pattern length**

```c
static bool s_match_mac(const filter_rule_t *rule, const proto_adv_report_t *report)
{
    uint8_t target[6];
    const char *p = rule->pattern;
    for (int i = 0; i < 6; i++) {
        // If pattern is "AA:BB" (too short), reads past null terminator!
    }
}
```

If someone adds a MAC filter with pattern `"AA:BB"` (too short), the function
reads past the null terminator into uninitialized memory. Fix:

```c
if (strlen(rule->pattern) != 17) {  // "XX:XX:XX:XX:XX:XX" = 17 chars
    return false;
}
```

**⚠️ Wildcard matching has exponential worst case**

Pattern `"*A*B*C"` against string `"AAAA...AAAA"` (30 A's) causes exponential
recursion. Bounded by 31-char max length in practice, but a malicious Lua
script could trigger it.

**⚠️ No `filter_remove_rule` function**

Can add rules and clear all, but can't remove a single rule. The CLI needs
`FILTER REMOVE <index>` — this function is missing.

---

## Module 4: Tests

### Coverage Summary
- ADV parser: 14 tests — normal, error, edge cases ✅
- JSON encoder: 11 tests — normal, error, escaping ✅
- Filter engine: 14 tests — all filter types, AND/OR logic ✅
- **Total: 39 tests, all passing**

### Missing Test Cases

| Module | Missing Test | Why It Matters |
|--------|-------------|----------------|
| adv_parser | UUID list with odd byte count (3 bytes) | Should handle gracefully |
| adv_parser | raw_len > 255 | Should reject per BLE spec |
| adv_parser | Multiple name AD structures | First should win |
| json_encoder | Report with ALL fields populated | Integration test |
| json_encoder | Control characters in name (\x01, \x1F) | Escaping correctness |
| filter_engine | MAC pattern with wrong length ("AA:BB") | Should not read past buffer |
| filter_engine | Pattern with multiple wildcards ("*A*B*") | Wildcard correctness |
| filter_engine | Empty pattern string "" | Edge case |

---

## Module 5: ESP-IDF Integration (`main.c`)

### Issues

**⚠️ `strcpy` without bounds check**

```c
strcpy(other.name, "RandomSpeaker");
```

Violates the project coding rule: "all array operations must be length-checked."
Should use `strncpy`.

**⚠️ Modifying report fields directly**

```c
report.rssi = -60;
```

In the real pipeline, reports come from BLE scan callback and should be
treated as read-only. Demo modifies the report directly.

**⚠️ No USB CDC implementation**

Uses `printf()` for output. Works because ESP32-S3 routes stdout to USB,
but the actual USB CDC console feature (F0.1) isn't implemented yet.

---

## Architecture-Level Concerns

| Concern | Severity | Detail |
|---------|----------|--------|
| No raw adv queue type | High | BLE callback needs a struct to pass raw data to pipeline via FreeRTOS queue |
| No thread safety | High | filter_engine_t needs mutex when accessed from multiple tasks |
| No pipeline stats | Medium | feature_scan_pipeline.md defines stats — not implemented |
| JSON missing fields | Medium | tx_power and flags parsed but not encoded — data loss |
| Component naming | Low | `json_enc` shadows ESP-IDF's built-in `json` component |

---

## Summary Scorecard

| Module | Correctness | Test Coverage | Code Quality | Ready for Integration? |
|--------|------------|---------------|-------------|----------------------|
| ADV Parser | Good (1 minor issue) | Good (14 tests) | Clean | Yes, add max-length check |
| JSON Encoder | Good (1 minor issue) | Good (11 tests) | Missing fields | Yes, add tx_power/flags |
| Filter Engine | 1 bug (MAC validation) | Good (14 tests) | Clean | Fix MAC bug first |
| Tests | 39/39 pass | Some gaps | Well-structured | Add missing edge cases |
| ESP-IDF main | Demo only | N/A | strcpy, mutable report | Needs USB CDC (F0.1) |

---

## Recommended Fixes (Priority Order)

1. **Fix `s_match_mac` pattern length validation** — potential out-of-bounds read
2. **Add `PROTO_ADV_DATA_MAX_LEN` check** in parser — enforce BLE spec limit
3. **Add `tx_power` and `flags` to JSON output** — data loss between parse and encode
4. **Check `s_encode_mac` return value** — code quality
5. **Replace `strcpy` with `strncpy` in main.c** — coding rule compliance
6. **Add missing test cases** — MAC wrong length, odd UUID bytes, control chars
