# ESP32-S3 BLE Sniffer Dongle

> Context-optimized project overview for AI coding agents. Root `README.md` has build/usage details; the user-facing guides live in `docs/`. New here? `docs/quickstart.md` is the 10-minute path.

## Architecture

ESP32-S3 passive BLE advertisement scanner → AD parse → C filters → optional Lua hooks → JSON lines over USB CDC → host PC tools. Stage 5 closes the product loop on the PC: an LLM analyzes the stream, answers questions, and writes Lua scripts deployed back over `SCRIPT LOAD`. Optional GATT-central connection plane (F2.4) re-streams peer data as `"src":"conn"` lines (analysis-only — conn lines deliberately bypass the Lua hooks).

**Three host tools (stage 5):**

| Tool | What it does | When to use |
|------|--------------|-------------|
| `llm_loop.py` (H5.1) | one-shot capture → LLM writes Lua → deploy → verify | scripted/batch runs |
| `host_app/run_case.py` (H5.2) | pluggable data-generator "cases" over the conn plane; `--estimate` checks the LLM against ground truth | demoing/verifying the connection plane end-to-end |
| `host_app/assistant.py` (H5.3) | interactive session: typed `answer\|lua\|clarify\|error` envelopes, human-confirmed deploys, `--system-extra/--system-file` prompt experiments | human-in-the-loop work, exploring the live data |

## Current Status: release — stages 1–6 complete

- All 13 firmware features (stages 1–4) + F2.4 conn plane + stage-5 host tooling implemented and hardware-verified
- **H6.1 Lua Tool Registry included**: tool packs (`manifest()` convention) + generate-and-execute, LittleFS pack persistence + boot autorun (`PACK` CLI family), `hw.*` device bindings behind `CONFIG_LUA_HW_BINDINGS` (gpio whitelist/ADC/kv configure-mode store), `LUA BEGIN/END` chunk exec, `--mutating-gate`, `--native-tools`
- Verification inventory: Unity 160 (response contract strict-JSON) · python assistant 153 + run_case 35 + llm_loop 10 · hw: conn 65/0, pack 13/13 (boot-autorun proof), hwio 18/18 (kv-reboot proof), `run_all_hw.py` gate 6/6 · plant demo 60/60, τ_est 9.5 vs 10.0 · soak green

## Key Rules

- **Reuse before building**: check ESP-IDF built-ins and the component registry first
- **malloc/free**: allowed for libraries; application logic uses static buffers (Lua runs on a static pool allocator)
- **Error codes**: module-specific ranges in `interfaces/*_if.h` (CLI -9xx, bridge -6xx/-8xx, storage -7xx, BLE -4xx, pipeline -8xx)
- **Lua sandbox**: whitelist libs only (base/string/table/math/utf8); uploads scanned **per line, fail-closed** for forbidden tokens (os./io./debug./require) → -612 (the rejection fires mid-upload, before `SCRIPT END`)
- **Concurrency**: `lua_State` behind `lua_engine_lock/unlock`; dedup table by spinlock; scan state atomic
- **Serial port (N3)**: closing COM12 resets the chip — keep the port open for the whole session; the final close at orderly exit is the accepted reset. `CONN STOP` returning ok means "terminate issued", not "state is off" — wait for `CONN STATUS state:"off"` before the next conn command
- **Host tools are self-contained by design** (copied helpers, no cross-imports); shared *conventions*, not shared code
- **LLM credentials** (`.llm_env`, gitignored): env vars win as a UNIT; the file may carry legacy `LLM_*` or provider pairs (`DASHSCOPE_*`, `TOKEN_PLAN_*`; model `LLM_MODEL`/`QWEN_MODEL`). The tools self-heal two known traps: a rejected shell key (401 → announced fallback to the file) and the Aliyun `/api/v1` native-dialect root (404 → announced retry on `/compatible-mode/v1`)
- **Never commit directly to the main branch**; one feature per branch, cut from a clean baseline; squash-merge, keep the branch as the revert path

## Build & Test

```
# Firmware (cmd/PowerShell)
scripts\build.bat && scripts\flash.bat

# Firmware (Git Bash — idf_cmd_init.bat breaks there; use this instead)
cmd //c "set MSYSTEM=&& set IDF_PATH=C:\Espressif\frameworks\esp-idf-v5.1&& set IDF_TOOLS_PATH=C:\Espressif\tools&& set PATH=C:\Espressif\tools\cmake\3.24.0\bin;C:\Espressif\tools\ninja\1.11.1;C:\Espressif\tools\xtensa-esp-elf\esp-13.2.0_20240530\xtensa-esp-elf\bin;C:\Espressif\tools\python_env\idf5.2_py3.12_env\Scripts;%PATH%&& C:\Espressif\tools\python_env\idf5.2_py3.12_env\Scripts\python.exe C:\Espressif\frameworks\esp-idf-v5.1\tools\idf.py build"
# append " -p COM12 flash" to flash (same env)

# C host tests (Unity; fresh dir, mingw + cmake on PATH)
export PATH="/c/msys64/mingw64/bin:/c/Espressif/tools/cmake/3.24.0/bin:$PATH"
cmake -S tests/host -B /tmp/hostbuild -G Ninja && cmake --build /tmp/hostbuild && /tmp/hostbuild/test_runner.exe
# 160 tests: unit suites + the response contract (every CLI response
# strict-JSON validated) + fuzz/boundary/scanner corpus

# Python unit tests (host tooling)
python tests/host/test_assistant.py    # 153 (tool registry, gates, native tools, whole REPL sessions)
python tests/host/test_run_case.py     # 35
python tests/host/test_llm_loop.py     # 10

# Hardware suites (dongle on COM12) — the one-command regression gate:
python tests/hw/run_all_hw.py          # whole battery + reset-reason gate + transcripts
# or individually:
python tests/hw/test_ble_conn_hw.py    # 65 checks (C0 + C1-C6 GATT tier + C7 state matrix)
python tests/hw/test_pack_hw.py         # H6.1 packs + boot autorun (reboot proof)
python tests/hw/test_hwio_hw.py         # H6.1 hw.* + kv configure-mode (reboot proof)
python tests/hw/test_ble_lua_hw.py     # BLE+Lua data plane
python tests/hw/test_bridge_hw.py      # F4.1+F4.2
python tests/hw/test_power_hw.py       # F4.3
python tests/hw/soak_conn.py --secs 3600 --reconnect-every 300   # conn soak

# Test docs: tests/README.md (inventory, conventions, evidence rules)
```

## Key Files

| Path | Purpose |
|------|---------|
| `interfaces/*_if.h` | Public C APIs — the only cross-module surface |
| `firmware/components/` | Implementation modules (usb, ble, proto, json_enc, filter, lua, cli, bridge, power, storage) |
| `main/main.c` | Init chain + USB command loop |
| `host_app/` + `llm_loop.py` | Stage-5 host tooling (self-contained scripts) |
| `tests/host/` | Unity C suite + python unit tests for the host tools |
| `tests/hw/` | pyserial/WinRT hardware suites (COM12) |
| `docs/features.zcode.md` | Feature guide (what the dongle can do) |
| `docs/user-guide.zcode.md` | Assistant & tool-pack user guide |
| `docs/demo-first-order.zcode.md` | Plant demo + host-side BLE simulation |
| `PowerPoint sharing/` | 17-slide architecture deck |
| `docs/quickstart.md` | 10-minute getting-started path |
