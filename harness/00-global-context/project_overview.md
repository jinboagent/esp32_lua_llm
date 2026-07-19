# Project Overview: ESP32-S3 BLE Sniffer Dongle

## Product

A USB dongle that passively scans BLE advertisements, parses them, applies user-defined filters/transforms, and outputs JSON lines over USB CDC serial. Target user: IoT developers building or debugging BLE-based IoT products.

On-device Lua 5.4 scripting enables custom filter and transform logic. A PC-side LLM loop can analyze captured traffic and generate Lua scripts that are deployed back to the device.

## Architecture

```
+---------------------+          +------------------------------------------+
|  PC / Host          |          |  ESP32-S3-DevKitC-1 (Device)             |
|                     |          |                                          |
|  +---------------+  |  USB CDC |  +--------+   +-------+   +----------+  |
|  | LLM (cloud)   |<-|--------->|  | BLE    |-->| AD    |-->| Filter   |  |
|  +---------------+  |  JSON     |  | Scan   |   | Parse |   | Engine   |  |
|         |           |  lines    |  +--------+   +-------+   +----------+  |
|    generates        |  <--->    |      |                           |      |
|    Lua scripts      |           |      v                      +----------+ |
|         |           |           |  +--------+   +-------+   | Pipeline |  |
|  +---------------+  |           |  | USB    |<--| JSON  |<--| (Lua +   |  |
|  | Script Deploy |<-|-----------|  | CDC    |   | Encode|   |  output) |  |
|  +---------------+  |  Lua src  |  +--------+   +-------+   +----------+ |
|                     |           |                                          |
|  +---------------+  |           |  +--------+                             |
|  | JSON Viewer / |  |           |  |LittleFS|  (script + config store)    |
|  | Analysis Tool |  |           |  +--------+                             |
|  +---------------+  |           |                                          |
+---------------------+          +------------------------------------------+
```

## Data Flow

```
BLE radio → NimBLE scan callback → copy raw ADV → FreeRTOS queue
  → AD structure parser → filter engine (C rules + optional Lua)
  → JSON encoder → USB CDC TX buffer → host
```

## LLM Loop

```
1. Device streams JSON lines to PC over USB CDC.
2. PC tool (or user) sends captured JSON to an LLM.
3. LLM analyzes advertisement patterns and generates a Lua filter/transform script.
4. Script is pushed back to the device over USB CDC (or stored via CLI).
5. Device loads script into Lua sandbox; pipeline applies it to subsequent reports.
```

## Host vs Device Responsibility

| Responsibility | Device | Host (PC) |
|---|---|---|
| BLE radio control & scanning | Yes | - |
| AD structure parsing | Yes | - |
| Filtering (C rules) | Yes | - |
| Lua script execution | Yes | - |
| JSON encoding & output | Yes | - |
| Script storage (LittleFS) | Yes | - |
| LLM analysis & script generation | - | Yes |
| Script deployment to device | - | Yes |
| Long-term capture storage | - | Yes |
| Visualization / dashboard | - | Yes |

## v1 Scope

**In scope:**
- Passive BLE advertisement scanning (all channels, 1M PHY)
- AD structure parsing (flags, name, UUID16/32/128, manufacturer data, TX power, appearance)
- C-based filter engine (MAC, name, RSSI, service UUID, manufacturer ID, AD type)
- Lua 5.4 sandbox for custom filter/transform
- JSON lines output over USB CDC
- CLI over USB CDC for configuration and script management
- LittleFS for script and config persistence
- Pipeline orchestration (scan → parse → filter → encode → output)

**Out of scope for v1:**
- Active scanning (scan request / scan response)
- BLE connections (GATT client/server)
- WiFi of any kind
- BLE mesh
- BLE direction finding / AoA
- Multi-PHY (coded PHY, 2M PHY)
- OTA firmware update
- BLE advertising (device does not advertise)

## Features Summary (13 features, 4 stages)

| Stage | # | Feature | Description |
|-------|---|---------|-------------|
| 1 - Foundation | 1 | Project scaffold | ESP-IDF project, CMake, directory layout, proto_if.h |
| 1 | 2 | USB CDC console | USB serial TX/RX, CLI command parsing |
| 1 | 3 | BLE scan | NimBLE passive scan, dedup, configurable interval/window |
| 1 | 4 | AD parser | Parse raw AD structures into typed fields |
| 2 - Pipeline | 5 | JSON encoder | Encode parsed ADV reports to JSON lines |
| 2 | 6 | Filter engine | C-based multi-rule filter (AND/OR logic) |
| 2 | 7 | Pipeline | Orchestrate scan → parse → filter → encode → output |
| 3 - Scripting | 8 | Lua engine | Lua 5.4 sandbox init, script load/execute |
| 3 | 9 | Lua filter integration | Pipeline calls Lua filter/transform per report |
| 3 | 10 | Script storage | LittleFS read/write/delete/list scripts |
| 4 - Polish | 11 | CLI commands | Full CLI: scan control, filter config, script mgmt, stats |
| 4 | 12 | State machine | IDLE → SCANNING → SCRIPT_RUNNING with guards |
| 4 | 13 | Integration test | End-to-end: scan → filter → Lua → JSON output verified |

## Hardware

| Parameter | Value |
|-----------|-------|
| MCU | ESP32-S3-WROOM-1 (N8R2 or N16R8) |
| Dev board | ESP32-S3-DevKitC-1 |
| Flash | 8 MB (N8) or 16 MB (N16) |
| PSRAM | 2 MB (N8R2) or 8 MB (N16R8) |
| BLE | Bluetooth 5.0 via internal radio + NimBLE |
| USB | USB-Serial/JTAG peripheral (USB CDC) |
| Clock | 240 MHz dual-core Xtensa LX7 |
| Power | USB bus-powered (~500 mA max) |

## Library Reuse Policy (MANDATORY)

Before designing ANY new module, the following ESP-IDF built-in components
MUST be evaluated for reuse. Only write custom code if:
1. The component does not exist in ESP-IDF or a well-known OSS project
2. The existing component cannot meet a hard requirement (state why)
3. The existing component's license is incompatible

### ESP-IDF Components — Always Check First
| Need | Existing Solution | Use It? |
|------|-------------------|---------|
| JSON encoding/decoding | `cJSON` (built-in) | ✅ Yes — replaces custom json_encoder.c |
| CLI commands | `esp_console` + `argtable3` (built-in) | ✅ Yes — replaces custom CLI parser |
| BLE AD parsing | NimBLE `ble_hs_adv_parse_fields()` | ✅ Yes — replaces custom proto_adv_parse.c |
| Filesystem | LittleFS (built-in) | Thin wrapper OK |
| USB CDC | TinyUSB (built-in) | Thin wrapper OK |
| Power management | `esp_pm` framework | ✅ Yes — use standard API |
| Scripting engine | No suitable built-in | ✅ Custom (Lua 5.4) |

### Redesign Required
The following custom modules should be **replaced** with ESP-IDF built-ins:
- `proto_adv_parse.c` → NimBLE `ble_hs_adv_parse_fields()`
- `json_encoder.c` → cJSON
- CLI (Stage 4) → `esp_console`

See `design_patch.md` for the full analysis and `status/status-2026-07-19-1400.md` for the current state.
