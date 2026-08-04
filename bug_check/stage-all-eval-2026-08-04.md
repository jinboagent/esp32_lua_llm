# 全阶段 Bug 状态 — 2026-08-04

> 上次评估后通过 3 个 fix commit 修复了 23 个 bug。当前剩余 28 个。

---

## 已修复 (23 个，commit: ee690ae, d79ff1f, 69ae6d9)

| ID | 阶段 | 描述 | Commit |
|----|:---:|------|--------|
| B1 | 0-1 | Pipeline vTaskDelete 死代码 → 改为 `for(;;)` 无限循环 | ee690ae |
| B2 | 0-1 | Filter 未接入 Pipeline → 添加 `pipeline_set_filter()` | ee690ae |
| B3 | 0-1 | NVS erase abort() 崩溃 → 手动错误检查 | ee690ae |
| B4 | 0-1 | USB getchar() 阻塞 → `fcntl(O_NONBLOCK)` | ee690ae |
| M2 | 0-1 | ftell -1 → uint32_t 溢出 → `file_size < 0` 检查 | d79ff1f |
| M4 | 0-1 | CLI 缺 FILTER ADD → 完整实现 | ee690ae |
| M5 | 0-1 | BLE 队列未清空 → `xQueueReset()` | ee690ae |
| L2 | 0-1 | fread 返回值未验证 → `read != file_size` 检查 | d79ff1f |
| L6 | 0-1 | \u00xx 小写 hex → `%02X` 大写 | d79ff1f |
| B-S2-1 | 2 | BLE sync timeout double-deinit 竞态 → `vTaskDelay(200)` | 69ae6d9 |
| B-S2-2 | 2 | `s_scanning` 双任务竞态 → `atomic_bool` | 69ae6d9 |
| B-S2-4 | 2 | 弱 XOR 哈希假去重 → FNV-1a + 线性探测 + LRU | 69ae6d9 |
| B-S3-1 | 3 | malloc/free 代替静态池 → bump+free-list pool allocator | 69ae6d9 |
| B-S3-2 | 3 | 沙箱黑名单旁路 → 白名单: 只开 base/string/table/math/utf8 | 69ae6d9 |
| B-S3-3 | 3 | 双重执行 (upload+run) → `lua_engine_compile_check()` 试编译 | 69ae6d9 |
| B-S3-4 | 3 | lua_State 无锁双任务访问 → `lua_engine_lock/unlock()` | 69ae6d9 |
| B-S3-5 | 3 | Hook func_name NULL 检查缺失 → 两个函数都加了检查 | 69ae6d9 |
| B-S3-6 | 3 | CLI+filter 并发竞态 → `lua_engine_lock` 包裹 filter 操作 | 69ae6d9 |
| B-S3-7 | 3 | SCAN START 不滚回 → `ble_scan_stop()` 在 pipeline 失败时调用 | 69ae6d9 |
| M-S3-1 | 3 | used-=osize 下溢 → pool allocator 不再有此问题 | 69ae6d9 |
| M-S3-2 | 3 | peak memory 标签错误 → `s_peak_used` 正确跟踪 | 69ae6d9 |
| L-S3-2 | 3 | package.path 未清空 → 白名单模式下 package 未加载 | 69ae6d9 |
| L-S3-3 | 3 | used=0 掩盖泄漏 → pool allocator 通过 s_pool_used 跟踪 | 69ae6d9 |

---

## 剩余 Bug (28 个)

### Stage 0-1 (6 个)

| ID | 严重度 | 文件 | 描述 |
|----|:------:|------|------|
| M1 | 🟠 | `usb_cdc_console.c:73-75` | USB 超时返回部分数据(正数)而非 -503 — 违反 API 契约 |
| M3 | 🟠 | `littlefs_storage.c:137-146` | `storage_file_exists` 不持锁 — 与 write/delete 竞态 |
| L1 | 🟡 | `ble_nimble_init.c:113-128` | `ble_deinit` 不先停扫描、不调 `nvs_flash_deinit` |
| L3 | 🟡 | `usb_cdc_console.c:12-28` | `usb_console_init` 未初始化 TinyUSB 硬件 (fcntl 足够) |
| L4 | 🟡 | `ble_scan.c:163-178` | `ble_scan_stop` 不排空/重置队列 |
| L5 | 🟡 | `filter_if.h:47` | `filter_rule_t.active` 死字段 — 始终为 true |

### Stage 2 — BLE (10 个)

| ID | 严重度 | 文件 | 描述 |
|----|:------:|------|------|
| B-S2-3 | 🔴 | `ble_scan.c:141` | Dedup memset 与 GAP 回调并发写入竞态 |
| M-S2-1 | 🟠 | `ble_nimble_init.c:81` | `ble_svc_gap_device_name_set()` 返回值未检查 |
| M-S2-2 | 🟠 | `ble_nimble_init.c:113-128` | `ble_deinit` 不先停扫描、不调 `nvs_flash_deinit` |
| M-S2-3 | 🟡 | `ble_scan.c:132` | 队列永不销毁 (设计决策，可接受) |
| M-S2-4 | 🟡 | `ble_scan.c:106` | queue-full drop 无计数器 — 不可观测 |
| M-S2-5 | 🟡 | `scan_pipeline.c` | 无 `pipeline_deinit()` (无限循环设计) |
| L-S2-1 | 🟡 | `ble_nimble_init.c:119-120` | 常规 deinit 不等待 host task 退出 |
| L-S2-2 | 🟡 | `ble_scan.c:144-146` | 扫描参数→NimBLE uint16_t 单位转换未校验 |
| L-S2-3 | 🟡 | `ble_scan.c:185-199` | `ble_scan_set_params` 允许扫描中修改参数 |
| L-S2-4 | 🟡 | `scan_pipeline.c:13` | 4096 字节栈 + ~1200 字节局部变量，无高水位检查 |

### Stage 3 — Lua (12 个)

| ID | 严重度 | 文件 | 描述 |
|----|:------:|------|------|
| M-S3-3 | 🟠 | `script_mgmt.c:150` | `script_stop` 不清除 Lua 全局表中的函数 |
| M-S3-4 | 🟠 | `lua_port.c:463-468` | Hook 调用错误一律返回 -613 — 不区分超时/OOM |
| M-S3-5 | 🟠 | `script_mgmt.c:52` | Tick→ms 乘法可能溢出 |
| M-S3-7 | 🟠 | `main.c:135` | RSSI `atoi` 无范围校验 (-128~+127) |
| M-S3-8 | 🟠 | `main.c:234-237` | SCRIPT CHUNK hex 解码无校验 |
| M-S3-6 | 🟡 | `lua_port.c:336` | Hook 每 100 指令触发 (spec 说 1000) |
| M-S3-9 | 🟡 | `scan_pipeline.c:78-79` | `on_adv` 只传 3 参数 (spec 要求 7) |
| L-S3-1 | 🟡 | `lua_port.c:195-217` | `string.dump` 在白名单 string 库中仍可用 |
| L-S3-4 | 🟡 | `lua_port.c:465,472` | 冗余 `lua_pop` 后 `lua_settop` |
| L-S3-5 | 🟡 | `script_mgmt.c:124` | `script_run` 每次从 LittleFS 重读 |
| L-S3-6 | 🟡 | `script_mgmt.c:87-91` | 不可达 else 分支 (无害) |

---

## 汇总

| 阶段 | 🔴 严重 | 🟠 中等 | 🟡 低 | 剩余 | 已修复 |
|------|:------:|:------:|:-----:|:----:|:------:|
| Stage 0-1 | 0 | 2 | 4 | **6** | 9 |
| Stage 2 | 1 | 2 | 7 | **10** | 3 |
| Stage 3 | 0 | 5 | 7 | **12** | 9 |
| Stage 4 | 4 | 7 | 5 | **16** | 0 |
| **总计** | **5** | **16** | **23** | **44** | **23** |

> 注: Stage 4 的 LLM Bridge (0%) 和 Power Management (0%) 仍完全未实现。CLI State Machine 仍缺失。
