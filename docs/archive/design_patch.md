  i would like to have a the opensource third party library to implement the feature as much as possible ? there are a lot lib in the esp32 project. 
then we need redesign the project and redesign the feature requirement . i can point out some reduncet  

1. The "reinventing the wheel" problem in the current design

Module-by-module analysis:

┌──────────────┬──────────────────────────────────────────────┬───────────────────────────────────────────────────────────┬──────────────────────────────────────┐
│    Module    │                Current Design                │                 What ESP-IDF Already Has                  │         Reinvents the Wheel?         │
├──────────────┼──────────────────────────────────────────────┼───────────────────────────────────────────────────────────┼──────────────────────────────────────┤
│  AD Parser   │       Hand-written AD structure parser       │   NimBLE ble_hs_adv_parse() / ble_hs_adv_parse_fields()   │                🔴 Yes                │
│              │                                              │             already fully parses all AD types             │                                      │
├──────────────┼──────────────────────────────────────────────┼───────────────────────────────────────────────────────────┼──────────────────────────────────────┤
│ JSON Encoder │   Hand-written snprintf JSON concatenation   │             ESP-IDF built-in cJSON component              │                🔴 Yes                │
├──────────────┼──────────────────────────────────────────────┼───────────────────────────────────────────────────────────┼──────────────────────────────────────┤
│ CLI Commands │ Hand-written command parsing + state machine │               ESP-IDF esp_console component               │                🔴 Yes                │
│              │                                              │           (with history, completion, argtable3)           │                                      │
├──────────────┼──────────────────────────────────────────────┼───────────────────────────────────────────────────────────┼──────────────────────────────────────┤
│   USB CDC    │         Hand-written USB CDC wrapper         │          ESP-IDF tusb / usb_serial_jtag drivers           │      🟡 Thin wrapper justified       │
├──────────────┼──────────────────────────────────────────────┼───────────────────────────────────────────────────────────┼──────────────────────────────────────┤
│   LittleFS   │     Hand-written file operation wrapper      │            ESP-IDF built-in LittleFS component            │      🟡 Thin wrapper justified       │
├──────────────┼──────────────────────────────────────────────┼───────────────────────────────────────────────────────────┼──────────────────────────────────────┤
│ NimBLE Init  │      Hand-written NimBLE initialization      │          NimBLE is a standard ESP-IDF component           │      🟡 Thin wrapper justified       │
├──────────────┼──────────────────────────────────────────────┼───────────────────────────────────────────────────────────┼──────────────────────────────────────┤
│   BLE Scan   │        Hand-written scanning + dedup         │     NimBLE scan API exists; the dedup logic is custom     │       🟡 Partially duplicated        │
├──────────────┼──────────────────────────────────────────────┼───────────────────────────────────────────────────────────┼──────────────────────────────────────┤
│  Lua Engine  │     Hand-written Lua 5.4 port + sandbox      │      No direct equivalent; but mature options exist:      │ 🔴 Design choice needs re-evaluation │
│              │                                              │    MicroPython (officially supported), WASM3, Duktape     │                                      │
├──────────────┼──────────────────────────────────────────────┼───────────────────────────────────────────────────────────┼──────────────────────────────────────┤
│  Power Mgmt  │    Hand-written power management wrapper     │ ESP-IDF esp_pm framework (light sleep / deep sleep / DFS) │       🟠 Partially duplicated        │
└──────────────┴──────────────────────────────────────────────┴───────────────────────────────────────────────────────────┴──────────────────────────────────────┘

Specifically:

AD Parser — a classic case of reinventing the wheel:

The current proto_adv_parse.c hand-writes a 136-line parser that only handles 7 AD types. NimBLE already provides:

// NimBLE built-in AD field parsing — covers all AD types defined by the BLE spec
int ble_hs_adv_parse(const struct ble_hs_adv_fields *fields, ...);
int ble_hs_adv_parse_fields(struct ble_hs_adv_fields *fields,
                             const uint8_t *data, uint8_t len);
// Auto-parses: flags, name, UUID16/32/128, TX power, manufacturer data,
//              service data, appearance, slave connection interval, etc.

The consequences of the hand-written parser have already surfaced in the evaluation: UUID32/128 not implemented, data silently dropped, and the need to maintain our own bounds-checking logic.

JSON Encoder — fragile hand-written concatenation:

The current json_encoder.c hand-assembles JSON with 163 lines of snprintf, and a stack-overflow bug has already been found in it. ESP-IDF's built-in cJSON library:

// cJSON — done in a few lines; structural correctness is guaranteed by the library
cJSON *root = cJSON_CreateObject();
cJSON_AddNumberToObject(root, "ts", report->ts_ms);
cJSON_AddStringToObject(root, "addr", mac_str);
// ...
char *json_str = cJSON_PrintUnformatted(root);  // single-line JSON

CLI — ignores esp_console:

The design document hand-writes a parser, state machine, and JSON response format for 16 commands. ESP-IDF's esp_console already provides:

// esp_console — built-in linenoise (history/completion/editing), argtable3 (argument parsing)
const esp_console_cmd_t cmd = {
    .command = "scan",
    .help = "Start BLE scanning",
    .hint = NULL,
    .func = &scan_command_handler,
    .argtable = &scan_arg_table,
};
ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
esp_console_run();  

you can do the following :
  set hard constraints in project_overview.md

## Library Reuse Policy (MANDATORY)

Before designing ANY new module, the following ESP-IDF built-in components
MUST be evaluated for reuse. Only write custom code if:
1. The component does not exist in ESP-IDF or a well-known OSS project
2. The existing component cannot meet a hard requirement (state why)
3. The existing component's license is incompatible

### ESP-IDF Components — Always Check First
| Need | Existing Solution |
|------|-------------------|
| JSON | `cJSON` (built-in) |
| CLI | `esp_console` + `argtable3` |
| BLE AD parsing | NimBLE `ble_hs_adv_parse()` |
| Filesystem | LittleFS / SPIFFS (built-in) |
| USB | TinyUSB (built-in) |
| Power | `esp_pm` |
| Scripting | Evaluate MicroPython first; Lua only if justified |
| Protocol buffers | `protobuf-c` / Nanopb |
| Compression | `miniz` (built-in) |

Method B: mandatorily add an "existing solutions survey" section to every Feature Doc

## Prior Art (MANDATORY — complete before writing implementation)

### 1. ESP-IDF Built-in
- [ ] Checked `$IDF_PATH/components/` for existing solutions
- [ ] Result: `cJSON` handles JSON encoding → **Use it, skip custom encoder**

### 2. ESP Component Registry
- [ ] Searched https://components.espressif.com for relevant components
- [ ] Result: Nothing additional needed

### 3. Justification for Custom Code
- [ ] If writing custom code, explain why existing solutions are insufficient:
  "NimBLE adv_parse doesn't expose field-level access the way we need..."

Method C: inject ESP-IDF component directory knowledge into the AI Prompt

When having the AI implement a module, first have it read the ESP-IDF component directory:

Before implementing the JSON encoder, search $IDF_PATH/components/ for
existing JSON libraries. Read the cJSON API docs. Only if cJSON cannot
meet the 512-byte single-line requirement, implement a custom encoder.
