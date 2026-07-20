# Status Report — 2026-07-20 22:35

## Project Overview
ESP32-S3 BLE sniffer dongle for IoT developers. Passive BLE advertisement scanning → JSON lines over USB CDC → host PC LLM analyzes data → generates Lua scripts → deploys back to device. 13 features across 4 stages.

## Current Status: Stage 0 COMPLETE ✅

### Latest Commit
```
e6ae8cd feat(stage0): add USB CDC console and LittleFS storage
```

### What's Working on Hardware
- ✅ USB CDC console: commands in, JSON responses out via USB-Serial/JTAG
- ✅ LittleFS storage: 57344 bytes free (56KB of 64KB partition)
- ✅ Command parser: STATUS, VERSION, SCAN START/STOP, FILTER CLEAR/LIST
- ✅ NimBLE AD parser (ble_hs_adv_parse_fields) + cJSON encoder + filter engine
- ✅ 39/39 host tests pass

### Verified Commands on Device
```
> STATUS
{"status":"ok","cmd":"status","scanning":false,"filter_count":0,"script_loaded":false,"script_running":false}

> VERSION
{"status":"ok","cmd":"version","firmware":"0.1.0","build_date":"Jul 20 2026","chip":"esp32s3"}
```

## Completed Milestones
| Milestone | Commit | Date |
|-----------|--------|------|
| Stage 1: Pure-C modules (parser, JSON, filter) | af058ba | Jul 19 |
| Design redirection: replace custom parser/JSON with ESP-IDF built-ins | af058ba | Jul 19 |
| Stage 0: USB CDC console + LittleFS storage | e6ae8cd | Jul 20 |

## Key Discoveries This Session

### LittleFS Component Registry
- Registry name is `joltwallet/littlefs` (NOT `espressif/esp_littlefs`)
- Component REQUIRES name is `littlefs` (NOT `esp_littlefs`)
- The `^` version constraint character is eaten by Windows cmd.exe escape — use `~1.14.0` instead
- Must delete `sdkconfig` file (not just `build/`) for sdkconfig.defaults changes to take effect

### USB Console
- `usb_serial_jtag_driver_install()` conflicts with ESP-IDF console — can't install twice
- Solution: use `stdin`/`stdout` (via `getchar()`/`printf()`) instead of driver API
- CRITICAL: `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y` + `CONFIG_ESP_CONSOLE_UART_DEFAULT=n` required for stdin to work over USB

### tmux Workflow
- User's tmux session name: `esp32` (NOT `build_infor`)
- Commands: `tmux send-keys -t esp32 'command' Enter`
- Capture: `tmux capture-pane -t esp32 -p -S -N`
- Must stop monitor (`Ctrl+]`) before flashing (COM port conflict)

## Pending Work
| Item | Priority | Blocked By |
|------|----------|-----------|
| Stage 2: Real BLE scanning with NimBLE | High | Ready to start |
| Stage 3: Lua 5.4 engine | High | Stage 2 |
| Stage 4: Full CLI + state machine + integration test | Medium | Stage 3 |
| Add manufacturer data back to test advertisement | Low | After Stage 2 |
| 22 deferred bugs from bug report | Low | After Stage 2 |

## Next Steps
1. **Stage 2 - BLE scan**: Implement real NimBLE passive scanning
   - Read `harness/01-features/stage2-ble-core/feature_ble_scan.md`
   - Replace hardcoded test data in main.c with live BLE scan results
   - Add dedup logic (same MAC, 1 report per second)
   - Wire: scan → parse → filter → JSON → USB output
2. **Stage 3 - Lua engine**: After BLE scan works
3. **Stage 4 - CLI + state machine**: After Lua works

## Build & Test Commands
```
# Host tests (PC)
set PATH=C:\msys64\mingw64\bin;%PATH%
cmake --build tests/host/build && tests/host/build/test_runner.exe

# ESP32 build/flash/monitor (via tmux session "esp32")
wsl -d Ubuntu bash -c "tmux send-keys -t esp32 '.\build.bat' Enter"
wsl -d Ubuntu bash -c "tmux send-keys -t esp32 '.\flash.bat' Enter"
wsl -d Ubuntu bash -c "tmux send-keys -t esp32 '.\monitor.bat' Enter"

# Stop monitor before flashing!
wsl -d Ubuntu bash -c "tmux send-keys -t esp32 C-]"

# Capture output
wsl -d Ubuntu bash -c "tmux capture-pane -t esp32 -p -S -15"
```

## Key Files
| File | Purpose |
|------|---------|
| `firmware/components/usb/` | USB CDC console (stdin/stdout) |
| `firmware/components/storage/` | LittleFS storage wrapper |
| `firmware/components/proto/` | NimBLE AD parser adapter |
| `firmware/components/json_enc/` | cJSON-based JSON encoder |
| `firmware/components/filter/` | C-based filter engine (AND/OR logic) |
| `interfaces/` | Public API headers (*_if.h) |
| `main/main.c` | Demo app with command parser |
| `sdkconfig.defaults` | Build configuration |
| `harness/` | Feature requirements, coding rules, knowledge base |
| `status/` | Timestamped status reports |
