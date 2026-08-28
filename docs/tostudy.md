# BLE Sniffer Dongle — Learning Roadmap

A structured study plan for building the ESP32-S3 BLE sniffer dongle.
Topics are ordered by priority — start from the top and work down.

---

## 1. ESP-IDF Foundations

**Why:** Everything in this project runs on ESP-IDF. You need to understand the build system, project structure, and how to flash/debug.

### What to learn
- ESP-IDF project structure: `CMakeLists.txt`, `sdkconfig`, `main/` component
- `idf.py` commands: `build`, `flash`, `monitor`, `set-target`, `menuconfig`
- Component model: how `components/` work, `idf_component_register()`
- Partition tables: what they are, how to customize for LittleFS
- `sdkconfig.defaults` and Kconfig system

### Resources
- [ESP-IDF Getting Started](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/)
- [ESP-IDF Build System](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-guides/build-system.html)
- [ESP-IDF Partition Tables](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-guides/partition-tables.html)

### Mini Project
Create a blank ESP-IDF project, build it, flash to your DevKitC-1, and see "Hello World" in the serial monitor.

```c
// main/main.c — minimal ESP-IDF app
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void app_main(void)
{
    printf("Hello from ESP32-S3!\n");
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
```

```cmake
# main/CMakeLists.txt
idf_component_register(SRCS "main.c" INCLUDE_DIRS ".")
```

```cmake
# CMakeLists.txt (project root)
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(hello_s3)
```

---

## 2. FreeRTOS Essentials

**Why:** The dongle uses 3 concurrent tasks (BLE, pipeline, USB CLI). You need to understand tasks, queues, and mutexes to make them work together without crashes.

### What to learn
- Tasks: `xTaskCreate()`, priorities, `vTaskDelay()`
- Queues: `xQueueCreate()`, `xQueueSend()`, `xQueueReceive()` — this is how BLE data flows to the pipeline
- Semaphores/Mutexes: `xSemaphoreCreateMutex()`, `xSemaphoreTake()`, `xSemaphoreGive()` — for protecting shared state like filter rules
- Notifications: `xTaskNotifyGive()` — lightweight alternative to queues for simple signals

### Resources
- [FreeRTOS on ESP32](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/system/freertos.html)
- [FreeRTOS Queue tutorial](https://www.freertos.org/Embedded-RTOS-Message-Queues.html)
- [ESP-IDF FreeRTOS examples](https://github.com/espressif/esp-idf/tree/master/examples/system/freertos)

### Mini Project
Create 2 tasks: Task A reads a counter and sends it via a queue. Task B receives from the queue and prints the value.

```c
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

static QueueHandle_t q;

void producer(void *arg)
{
    int count = 0;
    while (1) {
        xQueueSend(q, &count, portMAX_DELAY);
        count++;
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

void consumer(void *arg)
{
    int val;
    while (1) {
        xQueueReceive(q, &val, portMAX_DELAY);
        printf("Received: %d\n", val);
    }
}

void app_main(void)
{
    q = xQueueCreate(10, sizeof(int));
    xTaskCreate(producer, "prod", 2048, NULL, 5, NULL);
    xTaskCreate(consumer, "cons", 2048, NULL, 5, NULL);
}
```

---

## 3. BLE Fundamentals

**Why:** You need to understand what BLE advertisements look like "on the air" before you can parse them. This is the core data your dongle captures.

### What to learn
- BLE advertising: what is an advertisement? Why do devices broadcast?
- AD structure format: `[length] [type] [data...]` — this is what your parser processes
- Common AD types: Flags (0x01), UUIDs (0x03), Name (0x09), Manufacturer Data (0xFF)
- Address types: public vs random MAC addresses
- RSSI: what it means, how distance relates to signal strength
- Passive scan vs active scan: your dongle uses passive (no scan requests)

### Resources
- [BLE Advertising primer](https://www.bluetooth.com/blog/a-bluez-ble-advertising-primer/)
- [BLE AD types list](https://www.bluetooth.com/specifications/assigned-numbers/generic-access-profile/)
- [nRF Connect for Desktop](https://www.nordicsemi.com/Products/Development-tools/nrf-connect-for-desktop) — use this to scan real BLE devices and see what advertisements look like

### Mini Project
Use nRF Connect on your phone to scan for BLE devices around you. Look at the raw advertisement data. Try to identify:
- Which bytes are the device name
- Which bytes are the service UUIDs
- Which bytes are manufacturer data
- How the length/type/data structure works

---

## 4. NimBLE on ESP32

**Why:** NimBLE is the BLE stack you'll use. You need to understand how to initialize it, configure scanning, and receive advertisement callbacks.

### What to learn
- NimBLE architecture: host stack vs controller, HCI transport
- `esp_nimble_hci_init()` and NimBLE initialization sequence
- GAP (Generic Access Profile): scan parameters, scan start/stop
- `ble_gap_disc()`: how to start passive scanning
- Advertisement report callback: `ble_gap_adv_report_fn` — this is where raw bytes arrive
- Scan parameters: interval, window, filter policy

### Resources
- [ESP-IDF NimBLE example](https://github.com/espressif/esp-idf/tree/master/examples/bluetooth/nimble/bleprph)
- [NimBLE GAP API](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/bluetooth/nimble/index.html)
- [Mynewt NimBLE docs](https://mynewt.apache.org/latest/network/ble_sec/ble_hs/ble_hs.html)

### Example: Passive scan callback
```c
#include "nimble/ble.h"
#include "host/ble_hs.h"
#include "host/util/util.h"

static int gap_event_cb(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_DISC: {
        struct ble_gap_disc_desc *desc = &event->disc;
        // desc->addr = MAC address
        // desc->rssi = signal strength
        // desc->length_data = length of advertisement data
        // desc->data = raw advertisement bytes (this is what you parse!)
        printf("Found: RSSI=%d, len=%d\n", desc->rssi, desc->length_data);
        break;
    }
    }
    return 0;
}

// Start passive scanning
void start_scan(void)
{
    struct ble_gap_disc_params params = {0};
    params.passive = 1;  // passive scan only
    params.filter_duplicates = 0;
    ble_gap_disc(BLE_OWN_ADDR_PUBLIC, BLE_HS_FOREVER, &params, gap_event_cb, NULL);
}
```

---

## 5. USB CDC Serial

**Why:** This is how your dongle communicates with the PC. You need to send JSON lines out and receive commands in.

### What to learn
- ESP32-S3 native USB: TinyUSB stack, CDC-ACM class
- `tinyusb_cdcacm_write()` — send data to PC
- Read callback — receive commands from PC
- Line buffering: accumulate bytes until `\n`, then process as a command
- USB CDC vs UART: CDC uses the native USB port, no external chip needed

### Resources
- [ESP-IDF TinyUSB CDC example](https://github.com/espressif/esp-idf/tree/master/examples/peripherals/usb/device/tusb_serial_device)
- [TinyUSB documentation](https://docs.tinyusb.org/en/latest/)
- [ESP32-S3 USB pins](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-guides/usb.html)

### Example: USB CDC echo
```c
#include "tinyusb.h"
#include "tusb_cdc_acm.h"

static void on_cdc_rx(const uint8_t *buf, uint32_t len, void *arg)
{
    // Received data from PC — echo it back
    tinyusb_cdcacm_write(0, buf, len);
}

void app_main(void)
{
    // Initialize TinyUSB with CDC
    tinyusb_config_t cfg = { .descriptor = NULL };
    tinyusb_driver_install(&cfg);

    tinyusb_cdcacm_instance_t acm_cfg = {
        .usb_dev = TINYUSB_DEV_DEFAULT,
        .rx_unread_buf_sz = 256,
        .callback_rx = &on_cdc_rx,
    };
    tusb_cdc_acm_initialize(0, &acm_cfg, NULL);
}
```

---

## 6. LittleFS on ESP32

**Why:** You need a filesystem to store Lua scripts. LittleFS is designed for flash — wear-leveling, power-loss safe.

### What to learn
- Flash partition table: how to add a LittleFS partition
- `esp_littlefs_init()` — mount the filesystem
- File operations: open, read, write, close, delete, list directory
- Wear leveling: why it matters for flash storage

### Resources
- [esp_littlefs component](https://github.com/joltwallet/esp_littlefs)
- [LittleFS design paper](https://raw.githubusercontent.com/ARMmbed/littlefs/master/DESIGN.md)
- [ESP-IDF storage overview](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/storage/index.html)

### Example partition table (custom_partitions.csv)
```csv
# Name,   Type, SubType, Offset,  Size,   Flags
nvs,      data, nvs,     0x9000,  0x6000,
phy_init, data, phy,     0xf000,  0x1000,
factory,  app,  factory, 0x10000, 0x180000,
littlefs, data, spiffs,  0x190000,0x70000,
```

---

## 7. Lua 5.4 C API

**Why:** You need to embed a Lua interpreter in the ESP32 and expose C functions to Lua scripts. This is the most complex part of the project.

### What to learn
- Lua stack model: how C and Lua exchange values via a virtual stack
- `luaL_newstate()`, `luaL_openlibs()` — create and initialize a Lua VM
- `lua_pcall()` — call a Lua function from C
- `lua_register()` / `lua_pushcfunction()` — expose C functions to Lua
- Custom allocator: `lua_newstate()` with a memory-limited allocator
- Sandboxing: which libraries to load, which to block
- Lua source code: [lua-5.4 for ESP32](https://github.com/luaj/luaj) or compile from source

### Resources
- [Lua 5.4 Reference Manual](https://www.lua.org/manual/5.4/)
- [Programming in Lua (free online)](https://www.lua.org/pil/)
- [Lua C API tutorial](https://www.lua.org/pil/24.html)
- [Embedding Lua in C](https://www.lua.org/pil/25.html)

### Example: Embed Lua and call a function
```c
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"

// C function exposed to Lua
static int l_hello(lua_State *L)
{
    const char *name = luaL_checkstring(L, 1);
    printf("Hello from Lua: %s\n", name);
    lua_pushinteger(L, 42);  // return value
    return 1;
}

void run_lua(void)
{
    lua_State *L = luaL_newstate();
    luaL_openlibs(L);

    // Register C function
    lua_register(L, "hello", l_hello);

    // Run a Lua script
    const char *script = "print('Lua is running!') hello('ESP32')";
    if (luaL_dostring(L, script) != LUA_OK) {
        printf("Lua error: %s\n", lua_tostring(L, -1));
    }

    lua_close(L);
}
```

### Example: Memory-limited allocator
```c
// Limit Lua to 32KB of memory
#define LUA_MEM_LIMIT (32 * 1024)

static void *l_alloc(void *ud, void *ptr, size_t osize, size_t nsize)
{
    size_t *used = (size_t *)ud;
    if (nsize == 0) {
        // Free
        *used -= osize;
        free(ptr);
        return NULL;
    }
    if (*used - osize + nsize > LUA_MEM_LIMIT) {
        return NULL;  // Out of memory — Lua will throw an error
    }
    void *newptr = realloc(ptr, nsize);
    if (newptr) {
        *used = *used - osize + nsize;
    }
    return newptr;
}

lua_State *L = lua_newstate(l_alloc, &used_memory);
```

---

## 8. JSON Encoding in C

**Why:** You need to format BLE data as JSON lines without a JSON library. The encoder you already have (json_encoder.c) is a starting point.

### What to learn
- JSON format: objects, arrays, strings, numbers, null, escaping rules
- Manual string building with `snprintf()`
- String escaping: `"`, `\`, control characters → `\uXXXX`
- Buffer management: what happens when the buffer is too small

### Resources
- [JSON specification](https://www.json.org/json-en.html)
- [cJSON library (for reference)](https://github.com/DaveGamble/cJSON) — lightweight C JSON parser/generator

### Study the code you already have
Read `firmware/components/json_enc/json_encoder.c` — it's a working JSON encoder. Understand how it handles:
- Null fields (name → `"name":null`)
- String escaping (special characters in device names)
- Array formatting (UUID list)
- Buffer overflow protection

---

## 9. Embedded C Patterns

**Why:** These patterns appear throughout the firmware. Understanding them makes the code make sense.

### Ring Buffer (for BLE advertisement queue)
```c
typedef struct {
    uint8_t data[32][128];  // 32 slots, 128 bytes each
    uint8_t head;
    uint8_t tail;
    uint8_t count;
} ring_buf_t;

bool ring_push(ring_buf_t *rb, const uint8_t *data, uint8_t len);
bool ring_pop(ring_buf_t *rb, uint8_t *data, uint8_t *len);
```

### State Machine (for command processing)
```c
typedef enum { STATE_IDLE, STATE_SCANNING, STATE_SCRIPT_RUNNING } state_t;

state_t handle_command(state_t current, const char *cmd)
{
    switch (current) {
    case STATE_IDLE:
        if (strcmp(cmd, "SCAN START") == 0) return STATE_SCANNING;
        break;
    case STATE_SCANNING:
        if (strcmp(cmd, "SCAN STOP") == 0) return STATE_IDLE;
        break;
    // ...
    }
    return current;  // No state change
}
```

### Error Handling Pattern
```c
// Consistent error handling: check, log, return
int ret = some_function(arg);
if (ret != 0) {
    ESP_LOGE(TAG, "some_function failed: %d", ret);
    return ret;  // Propagate error up
}
```

### Resources
- [ESP-IDF Logging](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/system/log.html)
- [Design Patterns for Embedded Systems in C](https://www.amazon.com/dp/012374251X) (book reference)

---

## 10. LLM Integration (Host Side)

**Why:** The unique value of this product is the LLM loop. You need to understand how the host PC app works.

### What to learn
- Serial port programming on PC (Python `pyserial`)
- Reading JSON lines from serial
- Calling LLM APIs (OpenAI, Claude, etc.)
- Generating Lua scripts from LLM output
- Sending scripts back to the device

### Example: Python host script
```python
import serial
import json

ser = serial.Serial('COM4', 115200)

# Read JSON lines from dongle
while True:
    line = ser.readline().decode('utf-8').strip()
    if not line:
        continue
    try:
        data = json.loads(line)
        print(f"Device: {data.get('name', '?')} "
              f"RSSI: {data['rssi']} "
              f"UUIDs: {data.get('uuids', [])}")
    except json.JSONDecodeError:
        print(f"Command response: {line}")
```

---

## Study Schedule Suggestion

| Week | Topics | Goal |
|------|--------|------|
| 1 | ESP-IDF basics + FreeRTOS | Blink LED, create tasks, use queues |
| 2 | BLE fundamentals + NimBLE | Scan for BLE devices, print raw advertisement data |
| 3 | USB CDC + LittleFS | Send/receive text over USB, save/read files |
| 4 | Lua C API | Embed Lua, call C functions from Lua, sandbox |
| 5 | Integration | Wire everything together: scan → parse → JSON → USB |

---

## Quick Reference: Key ESP-IDF Commands

```bash
# Set target chip
idf.py set-target esp32s3

# Build
idf.py build

# Flash (default COM port)
idf.py -p COM4 flash

# Monitor serial output
idf.py -p COM4 monitor

# Build + flash + monitor
idf.py -p COM4 flash monitor

# Clean build
idf.py fullclean

# Menuconfig (SDK configuration)
idf.py menuconfig
```

## Quick Reference: Useful BLE Tools

| Tool | What it does |
|------|-------------|
| **nRF Connect** (phone) | Scan BLE devices, see raw advertisement data |
| **Wireshark + Ubertooth** | Capture BLE packets from the air |
| **ESP-IDF BLE examples** | Reference implementations for NimBLE |
| **btlejack** | BLE sniffer firmware for nRF52 dongles |

---

## 11. Toolchains and Cross-Compilation

**Why:** Understanding toolchains is essential for embedded development. You write code on one machine (PC) but it runs on a different processor (ESP32).

**Context:** We use two toolchains in this project — MinGW gcc for host-side testing and xtensa-esp32s3-elf-gcc for ESP32 firmware.

### What to learn
- What a toolchain is: compiler + assembler + linker + utilities
- Cross-compilation: compiling on one architecture for a different architecture
- Why you can't mix toolchains: x86_64 code won't run on Xtensa and vice versa
- Toolchain naming convention: `<arch>-<vendor>-<os>-<abi>-gcc`
- ESP-IDF toolchain: `xtensa-esp32s3-elf-gcc` (Xtensa architecture, ESP32-S3 target)

### Key Concepts
```
Your PC (x86_64)                    ESP32-S3 (Xtensa LX7)
┌─────────────┐                     ┌─────────────┐
│ MinGW gcc    │ ──► test.exe       │ xtensa-gcc   │ ──► firmware.bin
│ (native)     │   runs on PC       │ (cross)      │   runs on ESP32
└─────────────┘                     └─────────────┘
Same .c files, different toolchains, different outputs
```

### Resources
- [GCC Cross-Compilation Howto](https://gcc.gnu.org/wiki/How-to-cross-compile-GCC)
- [ESP-IDF Toolchain docs](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-guides/toolchain.html)

---

## 12. CMake Build System

**Why:** CMake generates the build instructions that tell the compiler what to do. Both our host tests and ESP32 firmware use CMake, but with different configurations.

**Context:** We have two CMakeLists.txt files — one for host tests (standard cmake) and one for ESP32 (ESP-IDF cmake with special functions).

### What to learn
- CMake is a build system GENERATOR — it doesn't compile, it creates build files
- `CMakeLists.txt` = input (you write), `build/` = output (generated)
- `idf_component_register()` — ESP-IDF's special cmake function for components
- Why `build/` is disposable: delete it and `idf.py build` recreates everything
- Input files (tracked in git): `CMakeLists.txt`, `sdkconfig.defaults`, `partitions.csv`
- Output files (gitignored): `build/`, `sdkconfig`, `*.bin`, `*.elf`, `*.o`

### Key Distinction
```
INPUT (you write)              OUTPUT (build generates)
─────────────────              ────────────────────────
CMakeLists.txt                 build/ble_sniffer.bin
sdkconfig.defaults             build/config/sdkconfig.h
partitions.csv                 build/partition_table/partition-table.bin
main/main.c                    build/esp-idf/*/lib*.a
firmware/components/*/*.c      build/bootloader/bootloader.bin
```

### Resources
- [ESP-IDF Build System](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-guides/build-system.html)
- [CMake Tutorial](https://cmake.org/cmake/help/latest/guide/tutorial/index.html)

---

## 13. USB JTAG Debugging

**Why:** When printf isn't enough, JTAG lets you pause the CPU, inspect variables, and step through code line by line. Essential for debugging crashes and complex logic bugs.

**Context:** Our ESP32-S3-DevKitC-1 has built-in USB JTAG. We haven't used it yet — serial monitor is sufficient for now. We'll need it for Stage 2 (BLE scan) and Stage 3 (Lua engine).

### What to learn
- JTAG vs serial monitor: JTAG can pause CPU and inspect memory; serial only reads text output
- When to use JTAG: crashes with no output, memory corruption, timing bugs, logic errors
- OpenOCD: the JTAG debug server (already installed with ESP-IDF)
- GDB: the debugger that connects to OpenOCD
- ESP32-S3 USB JTAG config: `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`

### Serial Monitor vs JTAG
```
Serial Monitor:                    JTAG Debugging:
printf() → read text               Pause CPU → inspect ALL memory
After code runs                    While code is running
Can't stop execution               Hit breakpoints, step line by line
Can't change values                Modify variables while paused
```

### Resources
- [ESP-IDF JTAG Debugging](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-guides/jtag-debugging/index.html)
- [ESP32-S3 USB JTAG setup](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-guides/usb.html)

---

## 14. tmux Workflow for ESP32 Development

**Why:** Organizing build/flash/monitor in tmux windows gives you live visibility into everything the AI agent is doing.

**Context:** We set up a tmux session `build_infor` that runs PowerShell. The AI agent sends commands via `wsl tmux send-keys` and reads output via `wsl tmux capture-pane`.

### What to learn
- tmux basics: sessions, windows, panes
- How to send commands: `tmux send-keys -t <session> "command" Enter`
- How to read output: `tmux capture-pane -t <session> -p -S -N`
- Stopping interactive programs: `Ctrl+]` for idf.py monitor
- Build scripts: `build.bat`, `flash.bat`, `monitor.bat` in project root

### Recommended tmux Layout
```
Window 0: Serial Monitor (screen /dev/ttyS12 115200)
Window 1: Build & Flash (.\build.bat, .\flash.bat)
Window 2: Code browsing (vim, less, cat)
```

### Resources
- [tmux cheat sheet](https://tmuxcheatsheet.com/)
- Project file: `harness/00-global-context/build_environment.md`

## 15. Conditional C Format Strings Cannot Quote One Branch Only

**Why:** The F2.4 `CONN TARGET` response built `"svc":"%s"` + a
runtime-chosen `,"chr":"` prefix + `%s}` — structurally unable to close
the chr quote only when chr is emitted, which produced invalid JSON for
every JSON-strict host tool while a lenient reader silently skipped it.

**Context:** The H5.2 plant demo died at CONN TARGET (2026-08-28); the
raw wire line showed `"chr":"…a01}`. The first repair attempt
(`"%s%s\"}"`) then broke the single-UUID form with a doubled quote —
caught only by the hw suite's C0 checks.

### What you just learned
- Split conditional formats into explicit branches; verify EVERY branch,
  not just the one your current flow exercises.
- printf format strings are concatenated at compile time — the branch
  logic has to live in the code, not inside the format.
- A serial reader that returns "the first status-looking line" can pass
  checks with stale lines from a previous command — match the response's
  identity (e.g. its `cmd` field) to the command you issued.
- `CONN STOP` returning ok means "terminate issued", not "state is off"
  — sequence against observed state.

### Resources
- `firmware/components/cli/cli_commands.c` (h_conn TARGET)
- `tests/hw/test_ble_conn_hw.py` (`cmd_during_scan`, `wait_conn_off`)
- `harness/02-knowledge/host-cases-2026-08-28.md`

## 16. Aliyun MaaS Endpoints: /compatible-mode/v1 vs /api/v1

**Why:** The `.llm_env` pointed at a dedicated Aliyun MaaS deployment
(`ws-….maas.aliyuncs.com`) via `/api/v1` and every chat request 404'd.

**Context:** After updating `.llm_env` (2026-08-28), probing showed
`GET /api/v1/models` working but `/chat/completions` missing — that root
speaks the native DashScope dialect. The OpenAI-compatible route on the
same host is `/compatible-mode/v1/chat/completions`.

### What to learn
- Same host, two protocols: `/api/v1/...` = native (input/output
  envelopes), `/compatible-mode/v1/...` = OpenAI-shaped.
- Diagnose 404-with-working-auth by probing `/models` on candidate
  roots before blaming the key.

### Resources
- `.llm_env` (base URL), `host_app/assistant.py` (`resolve_llm_config`)
## 17. C String Literals: Adjacent Concatenation and Escapes

**Why:** Host tests in this repo assert exact JSON output from C code;
reading those assertions (and writing them) requires fluency in how C
handles string literals.

**Context:** While reviewing the F2.4 merge-fix tests
(`tests/host/test_ble_conn.c`), the question came up what
`"{\"ts\":1,...\"src\":\"conn\","  "\"o\":{\"x\":1},...}"` means.

### What you just learned
- Two string literals separated only by whitespace are **concatenated
  at compile time into one string** — C has no literal continuation
  operator; you just close and reopen the quotes.
- `\"` is an escaped double quote *inside* the string; the string's own
  delimiters are the outermost `"`.
- A trailing comma inside the literal (e.g. after `"src":"conn",`) is
  JSON **content**, not C syntax — the real argument-separating comma
  sits before the next C expression.
- Where it's used: `TEST_ASSERT_EQUAL_STRING(expected, line)` compares
  byte-for-byte, so these literals pin the exact JSON the encoder
  emits (the 2026-08-28 merge-separator fix).

## 18. Structured LLM Output: Typed JSON Envelopes and Escaping

**Why:** The H5.3 assistant session asks a cloud LLM to reply in a
typed JSON envelope; the single most likely failure is invalid JSON
caused by Lua code inside a JSON string.

**Context:** Implementing and verifying `host_app/assistant.py`
(2026-08-28): the reply contract `{"type":"lua","text":"...","code":"..."}`
needs every newline as `\n`, quote as `\"`, backslash as `\` inside
`code` — exactly the escaping that LLMs most often get wrong.

### What to learn
- JSON string escaping rules (RFC 8259 §7): the six escapes + `\uXXXX`
- Defensive parsing patterns: strict parse → one corrective retry →
  fenced ` ```lua ` extraction fallback → clean error, never a crash
- Why "read the device protocol at the source" found two host bugs:
  per-line fail-closed sandbox scan (`-612` before `SCRIPT END`) and
  the `{"status":"error","code":N}` envelope shape — both only visible
  in `cli_commands.c` / `lua_llm_bridge.c`

### Resources
- `host_app/assistant.py` (`parse_envelope`, `upload_script`)
- `tests/host/test_assistant.py` (escaping round-trip, fenced fallback)
- `harness/02-knowledge/assistant-session-2026-08-28.md` (process report)
