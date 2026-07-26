  i would like to have a the opensource third party library to implement the feature as much as possible ? there are a lot lib in the esp32 project. 
then we need redesign the project and redesign the feature requirement . i can point out some reduncet  

1. 当前设计的"重复造轮子"问题

逐模块分析：

┌──────────────┬──────────────────────────┬────────────────────────────────────────────────────────────────────────────────┬─────────────────────────┐
│     模块     │         当前设计         │                                 ESP-IDF 已有的                                 │     是否重复造轮子      │
├──────────────┼──────────────────────────┼────────────────────────────────────────────────────────────────────────────────┼─────────────────────────┤
│ AD Parser    │ 手写 AD 结构解析器       │ NimBLE ble_hs_adv_parse() / ble_hs_adv_parse_fields() 已经完整解析所有 AD type │          🔴 是          │
├──────────────┼──────────────────────────┼────────────────────────────────────────────────────────────────────────────────┼─────────────────────────┤
│ JSON Encoder │ 手写 snprintf 拼接 JSON  │ ESP-IDF 内置 cJSON 组件                                                        │          🔴 是          │
├──────────────┼──────────────────────────┼────────────────────────────────────────────────────────────────────────────────┼─────────────────────────┤
│ CLI 命令     │ 手写命令解析 + 状态机    │ ESP-IDF esp_console 组件 (含历史、补全、argtable3)                             │          🔴 是          │
├──────────────┼──────────────────────────┼────────────────────────────────────────────────────────────────────────────────┼─────────────────────────┤
│ USB CDC      │ 手写 USB CDC 封装        │ ESP-IDF tusb / usb_serial_jtag 驱动                                            │      🟡 薄封装合理      │
├──────────────┼──────────────────────────┼────────────────────────────────────────────────────────────────────────────────┼─────────────────────────┤
│ LittleFS     │ 手写文件操作封装         │ ESP-IDF 内置 LittleFS 组件                                                     │      🟡 薄封装合理      │
├──────────────┼──────────────────────────┼────────────────────────────────────────────────────────────────────────────────┼─────────────────────────┤
│ NimBLE Init  │ 手写 NimBLE 初始化       │ NimBLE 是 ESP-IDF 标准组件                                                     │      🟡 薄封装合理      │
├──────────────┼──────────────────────────┼────────────────────────────────────────────────────────────────────────────────┼─────────────────────────┤
│ BLE Scan     │ 手写扫描 + 去重          │ NimBLE 扫描 API 存在，去重逻辑是定制的                                         │       🟡 部分重复       │
├──────────────┼──────────────────────────┼────────────────────────────────────────────────────────────────────────────────┼─────────────────────────┤
│ Lua 引擎     │ 手写 Lua 5.4 移植 + 沙箱 │ 无直接替代；但有 MicroPython（官方支持）、WASM3、Duktape 等成熟方案            │ 🔴 设计选择需要重新评估 │
├──────────────┼──────────────────────────┼────────────────────────────────────────────────────────────────────────────────┼─────────────────────────┤
│ Power Mgmt   │ 手写功耗管理封装         │ ESP-IDF esp_pm 框架（light sleep / deep sleep / DFS）                          │       🟠 部分重复       │
└──────────────┴──────────────────────────┴────────────────────────────────────────────────────────────────────────────────┴─────────────────────────┘

具体来说：

AD Parser — 典型的重复造轮子：

当前 proto_adv_parse.c 手写了 136 行解析器，只处理 7 种 AD type。而 NimBLE 已经提供：

// NimBLE 内置的 AD 字段解析——覆盖所有 BLE 规范定义的 AD type
int ble_hs_adv_parse(const struct ble_hs_adv_fields *fields, ...);
int ble_hs_adv_parse_fields(struct ble_hs_adv_fields *fields,
                             const uint8_t *data, uint8_t len);
// 自动解析: flags, name, UUID16/32/128, TX power, manufacturer data,
//           service data, appearance, slave connection interval, etc.

手写解析器的后果已经在评估中暴露：UUID32/128 未实现、静默丢弃数据、需要维护自己的边界检查逻辑。

JSON Encoder — 脆弱的手写拼接：

当前 json_encoder.c 用 163 行 snprintf 手工拼接 JSON，已经发现过栈溢出 bug。ESP-IDF 内置的 cJSON 库：

// cJSON — 几行代码完成，结构正确性由库保证
cJSON *root = cJSON_CreateObject();
cJSON_AddNumberToObject(root, "ts", report->ts_ms);
cJSON_AddStringToObject(root, "addr", mac_str);
// ...
char *json_str = cJSON_PrintUnformatted(root);  // 单行 JSON

CLI — 忽略了 esp_console：

设计文档手写了 16 个命令的解析器、状态机、JSON 响应格式。ESP-IDF 的 esp_console 已经提供：

// esp_console — 内置 linenoise (历史/补全/编辑)、argtable3 (参数解析)
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
  project_overview.md 中设置硬约束

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

方法 B：在每个 Feature Doc 中强制加入"已有方案调研"章节

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

方法 C：在 AI Prompt 中注入 ESP-IDF 组件目录知识

当让 AI 实现某个模块时，先让它读取 ESP-IDF 组件目录：

Before implementing the JSON encoder, search $IDF_PATH/components/ for
existing JSON libraries. Read the cJSON API docs. Only if cJSON cannot
meet the 512-byte single-line requirement, implement a custom encoder.
