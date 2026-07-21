# BLE Sniffer Dongle 项目代码评估报告

## 上下文

ESP32-S3 BLE Sniffer Dongle — 被动扫描 BLE 广播包、解析、过滤、JSON 输出的 USB 加密狗。v1 规划 13 个特性分 4 阶段，当前仅实现 Stage 1 的 3 个纯计算模块（AD 解析器、JSON 编码器、过滤引擎）+ 1 个静态 demo main.c。评估覆盖所有已存在的源代码、接口头文件、测试代码、构建配置和文档。

---

## 🔴 严重 Bug

### B1. JSON 编码器 — 栈缓冲区溢出 (`firmware/components/json_enc/json_encoder.c:86`)

```c
char escaped_name[PROTO_DEVICE_NAME_MAX_LEN * 2];  // = 64 字节
s_encode_string_escaped(escaped_name, sizeof(escaped_name), report->name);
```

- `s_encode_string_escaped` 对 `< 0x20` 的控制字符输出 `\uXXXX`（6 字节）
- BLE 设备名最长 31 字符，最坏情况: 31 × 6 + 1 = **187 字节** > 64 字节缓冲区
- 恶意/损坏的 BLE 设备在 AD name 中嵌入控制字符即可触发栈溢出
- **这是一个可被外部 BLE 无线电信号远程触发的漏洞**
- 修复：`escaped_name` 应调整为 `PROTO_DEVICE_NAME_MAX_LEN * 6` (192 字节)

### B2. 过滤引擎 — 通配符递归 DoS (`firmware/components/filter/filter_engine.c:60-86`)

- `s_wildcard_match` 使用递归实现，对 `*a*a*a*...*` 模式呈 O(2^n) 指数复杂度
- 攻击者通过 CLI 注入含 15 个 `*` 的过滤规则(max 31 字符)，配合 31 字符设备名
- 可阻塞 pipeline 任务超过 5 秒看门狗超时，导致系统复位
- 修复：在嵌入式场景下改用非递归实现

### B3. 分区表缺少 LittleFS 分区 (`partitions.csv`)

- 分区表只有 nvs/phy_init/factory，**没有任何 LittleFS/storage 分区**
- 这直接阻塞了: Lua 脚本存储 (F0.2)、Lua 引擎 (F3.1)、脚本管理 (F3.2)、CLI SCRIPT 命令 (F4.1)、LLM bridge (F4.2)
- `feature_littlefs_storage.md` 定义了 64KB LittleFS 分区，但分区表完全没有对应条目

### B4. sdkconfig.defaults 缺失关键配置

| 缺失项 | 影响 |
|--------|------|
| NimBLE (`CONFIG_BT_ENABLED`, `CONFIG_BT_NIMBLE_ENABLED`, `CONFIG_BT_NIMBLE_ROLE_OBSERVER`, `CONFIG_BT_NIMBLE_MEM_POOL_SIZE=70`) | BLE 无法初始化 |
| LittleFS (`CONFIG_LITTLEFS_ENABLED`) | 脚本存储不可用 |
| `CONFIG_WIFI_ENABLED=n` | 默认启用 WiFi，浪费 ~20KB RAM |
| `CONFIG_FREERTOS_SMP=n` | 双核 SMP 未按规范禁用 |
| CPU 频率 = 160MHz（规范要求 240MHz） | 吞吐量降低 |

---

## 🟠 中等严重度

### M1. UUID32/UUID128 解析缺失 (`proto_adv_parse.c`)

- `project_overview.md` v1 Scope 明确包含 "UUID16/32/128"
- `proto_adv_parse.c` 只处理 UUID16（AD types 0x02-0x03）
- AD types 0x04-0x07 的 `#define` 常量已定义但 switch 语句不处理，落入 `default: break`
- 属于功能未完成

### M2. JSON 编码器静默丢弃 `flags` 和 `tx_power` 字段

- 解析器正确填充 `has_flags`/`flags`/`has_tx_power`/`tx_power`
- 编码器从未输出这两个字段到 JSON
- 既非文档声明的限制，也非预期的省略 — 属于数据丢失路径

### M3. 错误码体系三套并行，互不兼容

| 来源 | NULL pointer 错误码 | 体系 |
|------|---------------------|------|
| `coding_rules.md` §1 | `-1` (`ERR_NULL_PTR`) | 全局通用码 |
| `proto_if.h` | `-102` | 模块范围 (-100~-199) |
| `filter_if.h` | `-302` | 模块范围 (-300~-399) |

- `filter_get_count` 返回 `-1`（超出 -300~-399 范围）
- `coding_rules.md` 的通用错误码表在实际代码中**完全未使用**

### M4. 解析器/过滤器静默截断，调用者无感知

- `s_parse_uuid16_list`: UUID > 10 个被丢弃，返回 0（成功）
- `s_parse_manufacturer_data`: 厂家数据 > 31 字节被丢弃，返回 0（成功）
- `filter_add_rule`: pattern ≥ 32 字符截断为 31，返回 0（成功）
- 调用者无从判断数据完整性

### M5. `filter_evaluate(NULL, ...)` 返回 `true`（放行）

- 程序员忘记 `filter_init` → 所有数据**静默绕过过滤**，不崩溃不报错
- 其他 filter 函数对 NULL 返回错误码，唯独 `filter_evaluate` 不一致
- 应返回 `false`（拦截）或使用 assert

### M6. 命名规范多处违规

- **枚举类型**应以 `_e` 结尾: `proto_addr_type_t` → 应为 `proto_addr_type_e`; `filter_type_t` → 应为 `filter_type_e`
- **静态函数**应加模块前缀: `s_parse_name` → 应为 `s_proto_parse_name`; `s_match_name` → 应为 `s_filter_match_name`; `s_encode_mac` → 应为 `s_json_encode_mac`
- `main.c` 使用 `printf` 而非规范要求的 `ESP_LOG*` 宏

### M7. `filter_clear` 与 `filter_init` 不一致

- `filter_init` 做 `memset(eng, 0, sizeof(filter_engine_t))`（全清零）
- `filter_clear` 只清零 `rules[]` 和 `rule_count`（部分清零）
- 当前等效，但若将来 `filter_engine_t` 新增字段（如 `bool enabled`），`filter_clear` 会漏掉

### M8. `filter_rule_t.active` 字段无公开 API

- 结构体有 `active` 字段，`s_evaluate_type` 也检查它
- 但没有 `filter_remove_rule()` 或 `filter_set_active()` API
- 该字段永远是 `true`（刚添加时设置），属于**无效接口表面积**

### M9. 接口头文件缺少 `extern "C"` 保护

- `proto_if.h`、`filter_if.h`、`json_if.h` 都没有 `#ifdef __cplusplus / extern "C" {` 块
- 项目宣称"host-testable"，若将来用 C++ 测试框架会链接失败

### M10. `test_main.c` 永远返回 0

- `main()` 执行 `UNITY_END()` 后无条件 `return 0`，丢弃 Unity 的失败计数
- CI 环境无法通过退出码检测测试失败

### M11. 仅 3/13 特性有实现代码

- 已实现: proto_adv_parse、filter_engine、json_encoder（均为纯计算模块）
- 以下特性**完全没有源文件**: BLE 扫描、NimBLE 初始化、Pipeline、Lua 引擎、脚本管理、CLI 命令、LLM Bridge、电源管理、USB CDC 控制台、LittleFS 存储

### M12. `coding_rules.md` 文档多处过期

| 过期内容 | 实际状态 |
|----------|----------|
| `proto_adv_report_t` 定义（含 `adv_data[62]`） | 实际结构体有已解析字段 |
| 过滤类型含 "manufacturer ID, AD type" | 只实现了 NAME/UUID/RSSI/MAC |
| BLE ADV raw buffer 64 字节 | `PROTO_ADV_DATA_MAX_LEN` = 31 |
| 任务优先级 Pipeline=10, CLI=5 | `feature_scan_pipeline.md` 说 Pipeline=2, USB=1 |

---

## 🟡 低严重度

### L1. 过滤引擎非线程安全

- `filter_add_rule`/`filter_evaluate`/`filter_clear` 读写共享状态，无锁无原子操作
- 三个 FreeRTOS 任务（BLE callback/pipeline/CLI）可能并发访问

### L2. `s_match_mac` 不验证 pattern 总长度

- 恰好读 17 字符，不检查是否还有额外字符，不检查 pattern 末尾是否为 `\0`
- `"AA:BB:CC:DD:EE:FF:extra"` 会被当作合法 MAC

### L3. hex 解析代码重复

- `s_match_uuid` 和 `s_match_mac` 中相同的 hex digit 解析逻辑 copy-paste
- 应提取为公共 `hex_digit()` 辅助函数

### L4. `ts_ms` 使用 `uint32_t` — 约 49.7 天回绕

- 设备启动后毫秒数，49.7 天后回绕到 0
- 长期运行可能时间戳排序错乱

### L5. `manu_id=0` 语义模糊

- `has_manu=false` 时 `manu_id` 默认为 0
- 0x0000 是合法的已分配 Company ID (Ericsson)
- 调用者忘记检查 `has_manu` 会读到看似有效的数据

### L6. 缺少 `const` 限定符

- `s_parse_uuid16_list(data, ...)` 和 `s_parse_manufacturer_data(data, ...)` 的 `data` 参数不修改，应为 `const uint8_t *`

### L7. 测试辅助函数缺少 `static`

- `s_make_report` (test_filter_engine.c) 和 `s_make_basic_report` (test_json_encoder.c) 未声明 `static`，有链接符号冲突风险

### L8. CMake 使用废弃的 `EXTRA_COMPONENT_DIRS`

- ESP-IDF v5.x 推荐使用项目根 `components/` 目录

### L9. 测试断言依赖 `strstr` 而非结构化验证

- `strstr(buf, "\"ts\":1000")` 会在 name 字段包含该子串时误判
- 应使用 JSON 解析器或精确字符串比较

### L10. `s_make_report` 中 `strncpy` 无显式 null 终止

- `strncpy(r.name, name, PROTO_DEVICE_NAME_MAX_LEN - 1)` 当 name ≥ 31 字符时不会写入 `\0`
- 当前测试使用短名称，但 helper 本身脆弱

---

## 🔵 架构/设计关注点

### A1. BLE 扫描核心逻辑完全未实现

- `main.c` 只是硬编码数据的静态 demo
- NimBLE 扫描回调、FreeRTOS 队列通信、pipeline 任务调度全部未开始
- 当前可用的仅在 host 端可独立测试的纯计算模块

### A2. 全部任务固定到 Core 0

- ESP32-S3 双核，Core 1 闲置
- BLE Host (priority 20) 可能长时间抢占 pipeline (priority 10)

### A3. 缺少 `_Static_assert` 编译期校验

- 结构体变更时无法自动检测 ABI 漂移

### A4. JSON 输出和日志共享 USB CDC 通道但无序列化

- 规范要求 pipeline 序列化，但 `main.c` 使用 `printf` 绕过 pipeline
- 主机端 JSON Lines 解析器遇到非 JSON 日志行会失败

---

## 📊 汇总

| 严重度 | 数量 | 关键项 |
|--------|------|--------|
| 🔴 严重 | 4 | JSON 栈溢出、通配符 DoS、缺少 LittleFS 分区、sdkconfig 缺失 |
| 🟠 中等 | 12 | UUID32/128 缺失、flags/tx_power 丢弃、错误码混乱、静默截断、NULL 放行、命名违规、`filter_clear` 不一致、`active` 死字段、`extern "C"` 缺失、test_main 退出码、实现覆盖率 3/13、文档过期 |
| 🟡 低 | 10 | 线程安全、MAC 验证、hex 重复代码、ts_ms 回绕、manu_id 模糊、const 缺失、static 缺失、CMake legacy、strstr 测试、strncpy 终止 |
| 🔵 架构 | 4 | 核心逻辑未实现、单核调度、无 `_Static_assert`、日志/JSON 混用 |

---

## 📋 建议修复优先级

1. **立即**: `json_encoder.c` escaped_name 缓冲区溢出、`s_wildcard_match` 非递归化
2. **尽快**: 补充 `partitions.csv` LittleFS 分区、`sdkconfig.defaults` NimBLE/LittleFS/WiFi/SMP 配置
3. **本阶段**: 统一错误码体系、补齐命名规范、修正 `test_main.c` 退出码、修复文档过期
4. **下阶段**: 实现 UUID32/128 解析、补齐 `flags`/`tx_power` JSON 输出、添加线程安全保护
5. **持续**: 补充测试覆盖（控制字符转义、递归深度、截断边界、NULL filter_evaluate 等）、修正 `strstr` 断言为结构化验证
