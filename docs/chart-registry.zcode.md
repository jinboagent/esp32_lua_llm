# Chart registry — every ASCII diagram, cataloged

> **Author:** zcode · 2026-09-26 · The project's ASCII charts are load-bearing
> documentation, so each one has an ID, a row in the tracking table, a snapshot
> with a plain-language explanation in the catalog, and a hash the checker can
> verify. Run `python scripts/check_charts.py` before merging anything that
> touches docs.

## Policy

- **ID = `CH-<file-slug>-<NN>`**, one per chart *site* (the location the chart
  was born at). If a file is renamed, update the Location column — the ID keeps
  its origin hint and never changes.
- **Role**: `master` = the maintained copy; `alias of CH-…` = a copy that must
  not drift from its master; `frozen copy of CH-…` = point-in-time record.
- **Status**: `current` (verified against code) · `stale` (known outdated,
  refresh me) · `untracked` (file not committed yet — the checker scans
  git-tracked files only, so the row verifies after the first commit) ·
  `frozen-proposal` / `historical` / `template` / `frozen` (exempt from the
  checker — these paths are never maintained).
- **Update rule**: changed a chart? paste the new chart into its catalog
  section, bump `Rev` + `Verified` + `sha8` in the tracking table, run the
  checker. Added a chart? add a `chart-id:` marker next to it, then a row,
  then a catalog section. Deleted one? remove row + section.
- **Exempt paths** (frozen records): `docs/archive/`, `status/archive/`,
  `docs/templates/`, `docs/tostudy.md`, `docs/feature-proposal-*`,
  `vendor_reference/`, vendored Lua sources.
- **Checker**: `scripts/check_charts.py` (`--scan` to list, `--all` to include
  frozen). Findings: `MODIFIED` (hash changed since review), `ORPHAN` (unmarked
  chart in a maintained path), `GONE` (marker survived, chart didn't),
  `STALE`, `NO-ROW`, `MISSING`.

## Tracking table

| ID | Role | Status | Location | Style | Keep in sync with | Verified | sha8 | Rev |
|----|------|--------|----------|-------|-------------------|----------|------|-----|
| CH-readme-01 | master | current | `README.md:40-61` | ascii-plus | system data path | 2026-09-26 | 26ed111d | 1 |
| CH-ovw-md-01 | master | current | `harness/00-global-context/project_overview.md:13-36` | ascii-plus | system blocks (product-spec view) | 2026-09-26 | bb3ff9c2 | 1 |
| CH-assist-py-01 | master | current | `host_app/assistant.py:9-13` | ascii-plus | assistant loop | 2026-09-26 | ce96ef25 | 1 |
| CH-assist-md-01 | alias of CH-assist-py-01 | current | `harness/01-features/stage5-host/feature_assistant_session.md:26-33` | unicode-box | assistant loop | 2026-09-26 | 61045fed | 1 |
| CH-diagrams-html-01 | master | untracked | `docs/diagrams/dongle_llm_loop_sequence.deepseek.html (mermaid)` | mermaid | full dongle-host-LLM sequence | frozen |  | - |
| CH-runcase-py-01 | master | current | `host_app/run_case.py:12-17` | unicode-box | conn plane / plant demo | 2026-09-26 | 02842e00 | 1 |
| CH-hostcases-md-01 | alias of CH-runcase-py-01 | current | `harness/01-features/stage5-host/feature_host_cases.md:27-33` | unicode-box | conn plane / plant demo | 2026-09-26 | 3320bdcd | 1 |
| CH-llmloop-md-01 | master | current | `harness/01-features/stage5-host/feature_llm_loop_tool.md:23-25` | flow-arrows | one-shot LLM loop | 2026-09-26 | 36dc8440 | 1 |
| CH-bridge-md-01 | master | current | `harness/01-features/stage4-integration/feature_lua_llm_bridge.md:23-35` | unicode-box | SCRIPT LOAD upload sequence | 2026-09-26 | 4fca1e38 | 1 |
| CH-bridgeif-h-01 | master | current | `interfaces/bridge_if.h:18-21` | ascii-plus | bridge handshake (C header) | 2026-09-26 | 3bba4d54 | 1 |
| CH-luascript-md-01 | master | current | `harness/01-features/stage3-lua/feature_lua_script_mgmt.md:59-72` | unicode-box | script_mgmt C-API sequence | 2026-09-26 | 8aab24ed | 1 |
| CH-cli-md-01 | master | current | `harness/01-features/stage4-integration/feature_cli_commands.md:28-38` | unicode-box | CLI state machine | 2026-09-26 | 5ce4b8b6 | 1 |
| CH-power-md-01 | master | current | `harness/01-features/stage4-integration/feature_power_management.md:34-38` | unicode-box | power state machine | 2026-09-26 | 708d0abc | 1 |
| CH-lfs-md-01 | master | current | `harness/01-features/stage0-base/feature_littlefs_storage.md:63-70` | tree | LittleFS layout | 2026-09-26 | 095ecde7 | 1 |
| CH-buildenv-md-01 | master | current | `harness/00-global-context/build_environment.md:13-26` | tree | Espressif tools tree | 2026-09-26 | 7d076175 | 1 |
| CH-buildenv-md-02 | master | current | `harness/00-global-context/build_environment.md:63-67` | tree | MSYS2 tools tree | 2026-09-26 | 79dbf211 | 1 |
| CH-gitwf-md-01 | master | current | `harness/00-global-context/git_workflow.md:277-283` | flow-arrows | branch model | 2026-09-26 | 559e79b9 | 1 |
| CH-ble-readme-01 | master | current | `firmware/components/ble/README.md:29-69` | mermaid | BLE tasks, queues, adv path | 2026-09-26 | 07fdeb4c | 1 |
| CH-ble-readme-02 | master | current | `firmware/components/ble/README.md:121-123` | flow-arrows | conn state machine | 2026-09-26 | 21d12ed3 | 1 |
| CH-ble-readme-03 | master | current | `firmware/components/ble/README.md:157-171` | mermaid | conn command sequence | 2026-09-26 | 2756f094 | 1 |
| CH-assist-prop-md-01 | frozen copy of CH-assist-py-01 | frozen-proposal | `docs/feature-proposal-assistant-session-2026-08-28.md:71-77` | unicode-box | assistant loop | frozen | 5d67e071 | - |
| CH-conn-prop-md-01 | frozen copy of CH-runcase-py-01 | frozen-proposal | `docs/feature-proposal-host-cases-2026-08-28.md:118-124` | unicode-box | conn plane / plant demo | frozen | 35b49189 | - |
| CH-midlayer-prop-md-01 | frozen | frozen-proposal | `docs/feature-proposal-lua-tool-registry-2026-08-29.zcode.md:109-118` | unicode-box | host middle layer (registry) | frozen | 5e20a81c | - |
| CH-planes-prop-md-01 | frozen | frozen-proposal | `docs/feature-proposal-lua-tool-registry-2026-08-29.zcode.md:124-139` | flow-arrows | PUSH vs PULL planes | frozen | 4687978f | - |
| CH-arch-qwen-md-01 | historical | historical | `docs/archive/qwen_featuer.md:22-31` | unicode-box | early system sketch | frozen | 34419383 | - |
| CH-depgraph-qwen-md-01 | historical | historical | `docs/archive/qwen_featuer.md:148-164` | flow-arrows | F0-F2 feature dependency graph | frozen | 766b06af | - |
| CH-tree-qwen-md-01 | historical | historical | `docs/archive/qwen_featuer.md:172-208` | tree | planned repo tree | frozen | 93234ab1 | - |
| CH-tree-status-md-01 | historical | historical | `status/archive/status-2026-07-19-0200.md:196-235` | tree | July repo tree snapshot | frozen | 2de9f531 | - |
| CH-matrix-design-md-01 | historical | historical | `docs/archive/design_patch.md:8-31` | unicode-box | module vs ESP-IDF built-ins | frozen | 7d7524e4 | - |
| CH-tmpl-state-md-01 | template | template | `docs/templates/feature_template.md:23-25` | flow-arrows | state-machine placeholder | frozen | a16ffd77 | - |
| CH-tmpl-tree-md-01 | template | template | `docs/templates/unit_test_template.md:164-171` | tree | test harness tree placeholder | frozen | 1b11277b | - |
| CH-toolchain-study-md-01 | frozen | frozen | `docs/tostudy.md:511-516` | unicode-box | MinGW vs xtensa toolchains | frozen | 3c742999 | - |
| CH-buildio-study-md-01 | frozen | frozen | `docs/tostudy.md:541-547` | tree | build inputs vs outputs | frozen | d78c64a7 | - |

Presence-only rows (sha8 empty) are verified by marker existence — their
rendering is not plain-text-detectable (mermaid HTML).

## Catalog

Every chart in the project, extracted. The snapshot shows the chart as last
verified; the tracking table's sha8 is what the checker recomputes.

### CH-readme-01 · System architecture — the canonical map

The whole product on one canvas: host side (collector/LLM tooling + test suites + the PC-simulated BLE peer) talks to the dongle over USB CDC; text commands go in, JSON advertisement lines come out.

*How to read:* Two columns = the two machines. `-->` and `<---` arrows carry data; the vertical flow on the device side is the scan pipeline (BLE → dedup → parse → filter → Lua hook → JSON → USB).

*Keep in sync with:* Any change to the scan pipeline, USB protocol, conn plane or host tools. If this and CH-ovw-md-01 diverge on purpose, note why here.

*Sites:* `README.md:40-61` — master

```
                            HOST PC
 +---------------------------------------------------------------------+
 |  collector / LLM tooling                test suites                  |
 |  - reads JSON adv lines                 - pyserial CLI drivers       |
 |  - analyzes, generates Lua              - bleak/WinRT controlled     |
 |  - SCRIPT LOAD upload                     BLE peer (PC advertises)   |
 +-------------------------- USB CDC (USB-Serial/JTAG) ----------------+
                    |   in: text commands (CR or LF; Ctrl+C = interrupt)
                    |   out: JSON lines (adv stream + command responses)
                    v
                            ESP32-S3 DONGLE
   NimBLE observer — passive, continuous discovery (BLE_HS_FOREVER)
        |  adv report: addr / addr_type / rssi / AD data / ts_ms
        v
   dedup (FNV-1a hash, ~1 s window, spinlock)  -->  raw ADV queue
        v
   pipeline task:
     AD parse (proto) -> C filter engine -> [Lua on_adv hook: keep/drop]
        v
     JSON encode (all dynamic text escaped) -> [Lua transform hook]
        v
     USB console TX (mutex-serialized lines)
```

### CH-ovw-md-01 · System architecture — product-spec variant

Same system as CH-readme-01 drawn for harness/00-global-context: LLM cloud ↔ host tools ↔ dongle internals (scan chain, GATT client, USB+CLI+bridge, Pipeline, LittleFS). Refreshed 2026-09-26 to include the F2.4 conn plane and the three host tools.

*How to read:* Left column = PC (LLM cloud, host tools, script deploy gate). Right column = dongle. The `GATT client (opt. F2.4)` box connects downward to a BLE peer; the note rows under the frame describe the conn plane.

*Keep in sync with:* Same triggers as CH-readme-01 — keep the two system views consistent.

*Sites:* `harness/00-global-context/project_overview.md:13-36` — master

```
+---------------------+          +------------------------------------------+ 
|  PC / Host          |          |  ESP32-S3-DevKitC-1 (Device)             | 
|                     |          |                                          | 
|  +---------------+  |  USB CDC |  +--------+   +-------+   +----------+  |  
|  | LLM (cloud)   |<-|--------->|  | BLE    |-->| AD    |-->| Filter   |  |  
|  +---------------+  |  JSON     |  | Scan   |   | Parse |   | Engine   |  | 
|         |           |  lines    |  +--------+   +-------+   +----------+  | 
|    analyzes         |  <--->    |      |                           |      | 
|    writes Lua       |           |      v                      +----------+ |
|         |           |           |  +--------+   +-------+   | Pipeline |  | 
|  +---------------+  |           |  | USB    |<--| JSON  |<--| (Lua +   |  | 
|  | Script Deploy |<-|-----------|  | CDC    |   | Encode|   |  output) |  | 
|  +---------------+  |  Lua src  |  +--------+   +-------+   +----------+ |  
|                     |           |                                          |
|  +---------------+  |           |  +--------+                             | 
|  | Host tools:   |  |           |  |LittleFS|  (scripts, packs, kv)       | 
|  | assistant.py  |  |           |  +--------+                             | 
|  | run_case.py   |  |           |                                          |
|  | llm_loop.py   |  |           |                                          |
|  +---------------+  |           |                                          |
+---------------------+          +------------------------------------------+ 
  F2.4 conn plane (optional): BLE Scan -> GATT client ->                      
    notify / poll -> "src":"conn" lines -> host; its own                      
    state machine lives in firmware/components/ble/README.md                  
```

### CH-assist-py-01 · Assistant loop — user, device, LLM

The interactive session: device JSON lines + your console input enter the message Prompt (ingress validation, bounded buffers), get assembled into one context (history + device snapshot), and the cloud LLM replies in typed envelopes.

*How to read:* `-->` data direction; the central box is assistant.py's Prompt stage; `(history + snapshot)` is the context assembly; `<-- typed JSON envelope` is the reply path.

*Keep in sync with:* Envelope types (answer/lua/clarify/error), buffer sizes, /commands, deploy gate.

*Sites:* `host_app/assistant.py:9-13`, `harness/01-features/stage5-host/feature_assistant_session.md:26-33` — master + 1 alias(es)

```
  ESP32 --USB JSON lines--> +--------------------+
 user  --console input---> | message Prompt     | --> context assembly --> cloud LLM
                           | ingress validation |     (history + snapshot)
                           | bounded buffers    | <-- typed JSON envelope
                           +--------------------+
```

### CH-diagrams-html-01 · Full dongle ↔ host ↔ LLM sequence (mermaid)

The most detailed chart in the repo: the complete loop as a sequence diagram — adv data plane vs LLM control plane, and the two paths out of an LLM envelope (tool-program result feedback vs hook artifact through the human gate).

*How to read:* Open the HTML in a browser (mermaid is inlined, renders offline). Self-contained by design — no CDN dependency.

*Keep in sync with:* Any assistant behavior change (envelopes, gates, tool registry flow).

*Sites:* `docs/diagrams/dongle_llm_loop_sequence.deepseek.html (mermaid)` — master

*(rendered diagram — see the site link above; not embeddable as text)*

### CH-runcase-py-01 · Conn plane — plant demo data flow

The first-order demo end to end: the PC simulates a plant, serves it over GATT notify, the dongle subscribes (or polls) and re-streams it as "src":"conn" lines into a capture file, and the LLM's estimate is graded against ground truth.

*How to read:* Left box = PC-side plant; right box = unchanged dongle firmware; the ▲ arrow marks auto-connect by CONN TARGET/START; the dashed-bottom arrow marks the re-streamed capture.

*Keep in sync with:* run_case.py case lifecycle, GattPeer UUIDs, conn plane behavior, --estimate grading.

*Sites:* `host_app/run_case.py:12-17`, `harness/01-features/stage5-host/feature_host_cases.md:27-33` — master + 1 alias(es)

```
  ┌──────────────────┐  GATT notify        ┌──────────────────────────┐
  │ y += (T/τ)(Ku−y) │ ──────────────────▶ │ ble_conn: subscribe,     │
  │ payload {"t","u","y"}                  │ json_encode_conn (merge) │
  └──────────────────┘                     └────────────┬─────────────┘
     ▲ CONN TARGET/START (auto) ────────────────────────┘  USB CDC
     └── "src":"conn" lines ──▶ capture JSONL ──▶ LLM (optional --estimate)
```

### CH-llmloop-md-01 · One-shot LLM loop

llm_loop.py in one line: scan, collect advertisement JSON, let the LLM write a Lua filter, deploy via SCRIPT LOAD/END, run it, and verify the cleaned stream — then loop.

*How to read:* Circular read: the bottom return arrow closes the loop (deployed filter changes what the next capture sees).

*Keep in sync with:* llm_loop.py subcommands, SCRIPT LOAD bridge, on_adv/transform hook ABI.

*Sites:* `harness/01-features/stage5-host/feature_llm_loop_tool.md:23-25` — master

```
  SCAN START ─▶ collect adv JSON ─▶ LLM generates Lua ─▶ SCRIPT LOAD/END
       ▲                                                       │
       └──────── clean JSON stream ◀─ on_adv/transform ◀─ SCRIPT RUN
```

### CH-bridge-md-01 · SCRIPT LOAD upload sequence

The deploy path wire-by-wire: SCRIPT LOAD handshake ("ready"), silent per-line buffering with the sandbox scan, SCRIPT END triggers luaL_loadstring validation, answer is "saved" or a compile error.

*How to read:* PC left, device right; `▶` requests and `◀` responses; annotations after the arrows are device-side actions.

*Keep in sync with:* bridge component behavior, -612 sandbox rejects, SCRIPT BEGIN/CHUNK/END variant.

*Sites:* `harness/01-features/stage4-integration/feature_lua_llm_bridge.md:23-35` — master

```
  PC                          Device
  │                              │
  │── SCRIPT LOAD ──────────────▶│  (CLI command)
  │◀── {"status":"ok",...} ─────│  respond "ready"
  │                              │
  │── script line 1 ────────────▶│  buffered internally
  │── script line 2 ────────────▶│
  │── ... ──────────────────────▶│
  │                              │
  │── SCRIPT END ───────────────▶│  (CLI command)
  │                              │  validate via luaL_loadstring()
  │◀── {"status":"ok",...} ─────│  respond "saved" or "compile error"
  │                              │
```

### CH-bridgeif-h-01 · Bridge handshake (C header form)

The same protocol as CH-bridge-md-01 in its most compact form, kept next to the C API it documents (bridge_upload_begin / handle_script_upload / upload_finish).

*How to read:* One row per exchange; right column names the C function that owns each step.

*Keep in sync with:* Any change to bridge_if.h function names or ordering.

*Sites:* `interfaces/bridge_if.h:18-21` — master

```
 *   |-- SCRIPT LOAD ------------->|  bridge_upload_begin  -> "ready"
 *   |-- script line 1 ----------->|  bridge_handle_script_upload (silent)
 *   |-- script line 2 ----------->|  ...
 *   |-- SCRIPT END -------------->|  bridge_upload_finish -> "saved"/error
```

### CH-luascript-md-01 · script_mgmt C-API call sequence

The chunked script upload at the C-API level: upload_begin opens a session, upload_chunk sends ≤512-byte pieces, upload_end finalizes and saves.

*How to read:* Strict request/response pairing — every call expects a 0 (or error) return before the next step.

*Keep in sync with:* script_mgmt API, chunk size limits, persistence layout.

*Sites:* `harness/01-features/stage3-lua/feature_lua_script_mgmt.md:59-72` — master

```
Host                          Dongle
  |                              |
  |--- script_upload_begin() -->|  Start upload session
  |<---------- 0 ---------------|
  |                              |
  |--- script_upload_chunk() -->|  Send chunk 1 (up to 512 bytes)
  |<---------- 0 ---------------|
  |                              |
  |--- script_upload_chunk() -->|  Send chunk 2
  |<---------- 0 ---------------|
  |          ...                 |
  |                              |
  |--- script_upload_end() ---->|  Finalize and save
  |<---------- 0 ---------------|
```

### CH-cli-md-01 · CLI state machine

The three CLI states (IDLE / SCANNING / SCRIPT_RUNNING), the transitions between them, and — added 2026-09-26 — what deliberately lives outside the machine (PACK, LUA, POWER, most of CONN) plus the Ctrl+C universal interrupt.

*How to read:* Horizontal arrows = guarded transitions (label names the command); the return rail at the bottom = the STOP commands walking states back.

*Keep in sync with:* cli_commands.c dispatch table, state guards (-911), new command families.

*Sites:* `harness/01-features/stage4-integration/feature_cli_commands.md:28-38` — master

```
 IDLE ──SCAN START──▶ SCANNING ──SCRIPT RUN──▶ SCRIPT_RUNNING
  ▲                     │                            │
  │   SCAN STOP         │   SCAN STOP               │   SCRIPT STOP
  └─────────────────────┴────────────────────────────┘

 outside this machine (no IDLE/SCANNING/SCRIPT_RUNNING guard):
   PACK BEGIN/LIST/RUN/DEL/AUTORUN · LUA INIT/EXEC/BEGIN/END · POWER *
   CONN TARGET/STATUS/STOP/INTERVAL — and CONN START, which is refused
   while SCRIPT_RUNNING; the conn plane has its own state machine
   (see firmware/components/ble/README.md)
 Ctrl+C (0x03) interrupts scan/script/upload/conn from any state.
```

### CH-power-md-01 · Power state machine

ACTIVE ↔ LIGHT_SLEEP on idle timeout / wake, plus the USB_SUSPEND side state on USB suspend/resume.

*How to read:* Three states, edge-labeled transitions; the ▼ branch is the suspend path.

*Keep in sync with:* power_mgmt behavior, PM locks, est_current_ma reporting.

*Sites:* `harness/01-features/stage4-integration/feature_power_management.md:34-38` — master

```
  ACTIVE ──(idle timeout)──▶ LIGHT_SLEEP ──(timer/USB)──▶ ACTIVE
     │                                                       │
     │  (USB suspend)                                        │
     ▼                                                       │
  USB_SUSPEND ──(USB resume)─────────────────────────────────┘
```

### CH-lfs-md-01 · LittleFS layout

The on-device filesystem tree, refreshed 2026-09-26 for stage 6: /littlefs/script.lua (the SCRIPT LOAD slot), /littlefs/packs/ (tool packs plus .autorun markers that load them at boot), /littlefs/kv/ (the hw.kv persistent store) and /littlefs/config/.

*How to read:* Plain directory tree with # annotations.

*Keep in sync with:* Storage component, PARTITIONS.csv, PACK CLI, hw.kv bindings.

*Sites:* `harness/01-features/stage0-base/feature_littlefs_storage.md:63-70` — master

```
/littlefs/
├── scripts/
│   ├── script1.lua
│   ├── script2.lua
│   └── ...
└── config/
    └── settings.json
```

### CH-buildenv-md-01 · Espressif tools tree

Where the ESP-IDF v5.1 toolchain lives on the Windows host and which wrapper to run first (idf_cmd_init.bat).

*How to read:* Tree annotated with ← comments; versions v5.1 (default) / v5.3.1 / v5.5 coexist.

*Keep in sync with:* Build environment changes, new ESP-IDF versions, tool paths in CLAUDE.md.

*Sites:* `harness/00-global-context/build_environment.md:13-26` — master

```
C:\Espressif\
├── idf_cmd_init.bat              ← Environment setup script (run this first)
├── frameworks\
│   ├── esp-idf-v5.1/             ← DEFAULT version (used by idf_cmd_init.bat)
│   ├── esp-idf-v5.3.1/           ← Newer version available
│   └── esp-idf-v5.5/             ← Newest version available
└── tools\
    ├── tools\cmake\3.24.0\       ← ESP-IDF bundled cmake
    ├── tools\cmake\3.30.2\       ← Also available
    ├── tools\ninja\1.10.2\       ← Build system
    ├── tools\xtensa-esp32s3-elf\ ← Cross-compiler for ESP32-S3
    ├── tools\xtensa-esp-elf\     ← Generic Xtensa toolchain
    ├── tools\riscv32-esp-elf\    ← RISC-V toolchain (ESP32-C3 etc.)
    └── python_env\               ← Python virtualenv for IDF tools
```

### CH-buildenv-md-02 · MSYS2 tools tree

The native MinGW gcc/cmake/make used for host builds of the Unity suite.

*How to read:* Tree annotated with ← comments.

*Keep in sync with:* Host test build instructions.

*Sites:* `harness/00-global-context/build_environment.md:63-67` — master

```
C:\Espressif\
├── idf_cmd_init.bat              ← Environment setup script (run this first)
├── frameworks\
│   ├── esp-idf-v5.1/             ← DEFAULT version (used by idf_cmd_init.bat)
│   ├── esp-idf-v5.3.1/           ← Newer version available
│   └── esp-idf-v5.5/             ← Newest version available
└── tools\
    ├── tools\cmake\3.24.0\       ← ESP-IDF bundled cmake
    ├── tools\cmake\3.30.2\       ← Also available
    ├── tools\ninja\1.10.2\       ← Build system
    ├── tools\xtensa-esp32s3-elf\ ← Cross-compiler for ESP32-S3
    ├── tools\xtensa-esp-elf\     ← Generic Xtensa toolchain
    ├── tools\riscv32-esp-elf\    ← RISC-V toolchain (ESP32-C3 etc.)
    └── python_env\               ← Python virtualenv for IDF tools
```

### CH-gitwf-md-01 · Branch model

The git workflow: features branch off main, commit, and squash-merge back; tags mark releases.

*How to read:* Two swimlanes = two parallel feature branches; ↓ merge (squash) marks the merge points.

*Keep in sync with:* docs workflow policy; branch/merge rituals.

*Sites:* `harness/00-global-context/git_workflow.md:277-283` — master

```
main ──────────────────────────────────────────────────────►
  \                                    \
   feature/1-project-scaffold           feature/3-lua-engine
   [commits]                            [commits]
   ↓ merge (squash)                     ↓ merge (squash)
main ──────────────────────────────────────────────────────►
                                        tag: v0.3.0
```

### CH-ble-readme-01 · BLE tasks, queues and the adv path (mermaid)

The ble component's concurrency map: NimBLE host / main CLI / pipeline / conn_worker tasks and the queues between them (s_scan_queue 32-deep, s_op_q, s_rx_q), plus the advertisement data path to USB.

*How to read:* Rendered as a mermaid flowchart (GitHub renders it inline); cylinders are queues, rectangles tasks.

*Keep in sync with:* Queue depths, task priorities, any new ble component task or queue.

*Sites:* `firmware/components/ble/README.md:29-69` — master

```
flowchart LR
    subgraph radio["BLE radio / controller"]
    end

    HOST[["NimBLE host task<br/>(nimble_port_run)"]]
    MAIN[["main / CLI task<br/>(app_main command loop)"]]
    PIPE[["pipeline task<br/>(core 0, prio 2)"]]
    CONN[["conn_worker task<br/>(core 0, prio 2)"]]

    SCANQ[("s_scan_queue<br/>(32×adv_report)")]
    OPQ[("s_op_q<br/>(4×conn_op)")]
    RXQ[("s_rx_q<br/>(8×conn_rx)")]
    MUX{{"s_mux<br/>(conn state)"}}
    DEDUP{{"s_dedup_mux<br/>(spinlock)"}}
    STSEM(("s_start_sem<br/>(binary)"))
    TXM{{"s_tx_mutex<br/>(USB TX)"}}
    USB[("USB CDC")]

    radio --> HOST

    HOST -->|"DISC: tap + dedup<br/>(s_dedup_mux)"| SCANQ
    HOST -->|"GAP/GATT events"| MUX
    HOST -->|"notify/read payloads"| RXQ
    HOST -.->|"s_tap (fn ptr)"| CONN
    HOST -.->|"s_event_cb → power_hold_conn"| MAIN

    SCANQ --> PIPE
    PIPE -->|"usb_console_send_json"| TXM
    TXM --> USB

    CONN -->|"drain"| RXQ
    CONN -->|"usb_console_send_json"| TXM
    CONN -->|"posts on direct start"| STSEM

    MAIN -->|"SCAN/CONN/PIPELINE cmds"| MUX
    MAIN -->|"CONN START ops"| OPQ
    MAIN -.->|"blocks on"| STSEM
    MAIN -->|"read_line (RX only)"| USB

    MUX -->|"state"| CONN
    OPQ --> CONN
```

### CH-ble-readme-02 · Conn state machine

The conn plane's own state machine: off → peer_search → connecting → discovering → active, with timeout / cancel / fail edges back to off.

*How to read:* Linear happy path on top; the bottom rail collects all failure edges.

*Keep in sync with:* ble_conn.c states, CONN command family, C7 state-matrix test.

*Sites:* `firmware/components/ble/README.md:121-123` — master

```
flowchart LR
    subgraph radio["BLE radio / controller"]
    end

    HOST[["NimBLE host task<br/>(nimble_port_run)"]]
    MAIN[["main / CLI task<br/>(app_main command loop)"]]
    PIPE[["pipeline task<br/>(core 0, prio 2)"]]
    CONN[["conn_worker task<br/>(core 0, prio 2)"]]

    SCANQ[("s_scan_queue<br/>(32×adv_report)")]
    OPQ[("s_op_q<br/>(4×conn_op)")]
    RXQ[("s_rx_q<br/>(8×conn_rx)")]
    MUX{{"s_mux<br/>(conn state)"}}
    DEDUP{{"s_dedup_mux<br/>(spinlock)"}}
    STSEM(("s_start_sem<br/>(binary)"))
    TXM{{"s_tx_mutex<br/>(USB TX)"}}
    USB[("USB CDC")]

    radio --> HOST

    HOST -->|"DISC: tap + dedup<br/>(s_dedup_mux)"| SCANQ
    HOST -->|"GAP/GATT events"| MUX
    HOST -->|"notify/read payloads"| RXQ
    HOST -.->|"s_tap (fn ptr)"| CONN
    HOST -.->|"s_event_cb → power_hold_conn"| MAIN

    SCANQ --> PIPE
    PIPE -->|"usb_console_send_json"| TXM
    TXM --> USB

    CONN -->|"drain"| RXQ
    CONN -->|"usb_console_send_json"| TXM
    CONN -->|"posts on direct start"| STSEM

    MAIN -->|"SCAN/CONN/PIPELINE cmds"| MUX
    MAIN -->|"CONN START ops"| OPQ
    MAIN -.->|"blocks on"| STSEM
    MAIN -->|"read_line (RX only)"| USB

    MUX -->|"state"| CONN
    OPQ --> CONN
```

### CH-ble-readme-03 · Conn command sequence (mermaid)

CONN START as a task-level sequence: CLI → op queue → conn_worker (pause scan, ble_gap_connect with EBUSY retry) → host task discovery → CCCD/read — the mechanics behind the state machine.

*How to read:* Mermaid sequenceDiagram; `Note over` marks blocking points (s_start_sem).

*Keep in sync with:* conn_worker implementation, op queue, retry logic.

*Sites:* `firmware/components/ble/README.md:157-171` — master

```
flowchart LR
    subgraph radio["BLE radio / controller"]
    end

    HOST[["NimBLE host task<br/>(nimble_port_run)"]]
    MAIN[["main / CLI task<br/>(app_main command loop)"]]
    PIPE[["pipeline task<br/>(core 0, prio 2)"]]
    CONN[["conn_worker task<br/>(core 0, prio 2)"]]

    SCANQ[("s_scan_queue<br/>(32×adv_report)")]
    OPQ[("s_op_q<br/>(4×conn_op)")]
    RXQ[("s_rx_q<br/>(8×conn_rx)")]
    MUX{{"s_mux<br/>(conn state)"}}
    DEDUP{{"s_dedup_mux<br/>(spinlock)"}}
    STSEM(("s_start_sem<br/>(binary)"))
    TXM{{"s_tx_mutex<br/>(USB TX)"}}
    USB[("USB CDC")]

    radio --> HOST

    HOST -->|"DISC: tap + dedup<br/>(s_dedup_mux)"| SCANQ
    HOST -->|"GAP/GATT events"| MUX
    HOST -->|"notify/read payloads"| RXQ
    HOST -.->|"s_tap (fn ptr)"| CONN
    HOST -.->|"s_event_cb → power_hold_conn"| MAIN

    SCANQ --> PIPE
    PIPE -->|"usb_console_send_json"| TXM
    TXM --> USB

    CONN -->|"drain"| RXQ
    CONN -->|"usb_console_send_json"| TXM
    CONN -->|"posts on direct start"| STSEM

    MAIN -->|"SCAN/CONN/PIPELINE cmds"| MUX
    MAIN -->|"CONN START ops"| OPQ
    MAIN -.->|"blocks on"| STSEM
    MAIN -->|"read_line (RX only)"| USB

    MUX -->|"state"| CONN
    OPQ --> CONN
```

### CH-assist-prop-md-01 · Assistant loop — proposal copy

Frozen proposal-era copy of the assistant loop (the origin of CH-assist-py-01). The live chart is maintained at CH-assist-py-01 / CH-assist-md-01.

*Sites:* `docs/feature-proposal-assistant-session-2026-08-28.md:71-77` — master

```
ESP32 ──USB JSON lines──┐
                        ▼                    ┌────────────────┐
user ──console input──▶ MESSAGE Prompt ──────▶ │ context        │──▶ cloud LLM
                        · ingress validation │ assembly:      │
                        · bounded buffers    │ history +      │
                        · routes typed       │ device snapshot│
                          replies back       └────────────────┘
```

### CH-conn-prop-md-01 · Conn plane — proposal copy

Frozen proposal-era copy of the conn-plane flow (origin of CH-runcase-py-01).

*Sites:* `docs/feature-proposal-host-cases-2026-08-28.md:118-124` — master

```
 PC plant thread-in-main-loop                dongle (unchanged firmware)
 ┌──────────────┐  GATT notify 1 Hz   ┌──────────────────────────────┐
 │ y += (T/τ)(Ku−y)│ ───────────────▶ │ ble_conn: subscribe,         │
 │ payload {"t","u","y"}│              │ json_encode_conn (merge)    │
 └──────────────┘                      └──────────────┬───────────────┘
     ▲ CONN TARGET/START (auto) ──────────────────────┘  USB CDC
     └── "src":"conn" lines ── collect ──▶ plant_capture.jsonl ──▶ LLM (optional)
```

### CH-midlayer-prop-md-01 · Host middle layer (tool registry)

The registry design sketch: user ↔ host runtime (manifest injection, execute + results, clarify relay) ↔ LLM ↔ device LUA EXEC/packs. Still an accurate picture of how the registry sits between user and model.

*Sites:* `docs/feature-proposal-lua-tool-registry-2026-08-29.zcode.md:109-118` — master

```
 user ◄──clarify / results──► ┌──────────────────────┐
   intent, purpose, answers   │  host python script  │  ← the middle layer:
                              │  (assistant/agent    │    manifest injection,
                              │   runtime)           │    execute + results,
                              │                      │    clarify relay,
        prompt + manifest ▲   │  policy & loop       │    policy enforcement
        generated Lua  ┌─────┴──────┐                │
        results ──────►│    LLM     │◄───────────────┘
                       └────────────┘        LUA EXEC / packs
                                    device ◄──────────┘
```

### CH-planes-prop-md-01 · PUSH vs PULL planes

The key mental model: the PUSH plane (device initiates: scan pipeline calls your Lua, results stream out) vs the PULL plane (host/LLM initiates: LUA EXEC tool calls). Still accurate.

*Sites:* `docs/feature-proposal-lua-tool-registry-2026-08-29.zcode.md:124-139` — master

```
 user ◄──clarify / results──► ┌──────────────────────┐
   intent, purpose, answers   │  host python script  │  ← the middle layer:
                              │  (assistant/agent    │    manifest injection,
                              │   runtime)           │    execute + results,
                              │                      │    clarify relay,
        prompt + manifest ▲   │  policy & loop       │    policy enforcement
        generated Lua  ┌─────┴──────┐                │
        results ──────►│    LLM     │◄───────────────┘
                       └────────────┘        LUA EXEC / packs
                                    device ◄──────────┘
```

### CH-arch-qwen-md-01 · Early system sketch (historical)

The original 2026-07 host/dongle sketch — kept as history, superseded by CH-readme-01.

*Sites:* `docs/archive/qwen_featuer.md:22-31` — master

```
┌─────────────────┐      USB CDC (JSON lines)      ┌──────────────────┐
│   Host PC        │ ◄───────────────────────────── │   ESP32-S3       │
│                  │     BLE advertisement stream    │   Dongle         │
│   LLM analyzes   │                                │                  │
│   BLE data       │                                │   Captures BLE   │
│                  │ ──────────────────────────────► │   advertisements │
│   LLM generates  │     Lua script (text)          │                  │
│   Lua script     │     (deployed to device)       │   Executes Lua   │
│                  │                                │   filter/transform│
└──────────────────┘                                └──────────────────┘
```

### CH-depgraph-qwen-md-01 · Feature dependency graph (historical)

Which stage-0..2 features depended on which — the build-order plan.

*Sites:* `docs/archive/qwen_featuer.md:148-164` — master

```
┌─────────────────┐      USB CDC (JSON lines)      ┌──────────────────┐
│   Host PC        │ ◄───────────────────────────── │   ESP32-S3       │
│                  │     BLE advertisement stream    │   Dongle         │
│   LLM analyzes   │                                │                  │
│   BLE data       │                                │   Captures BLE   │
│                  │ ──────────────────────────────► │   advertisements │
│   LLM generates  │     Lua script (text)          │                  │
│   Lua script     │     (deployed to device)       │   Executes Lua   │
│                  │                                │   filter/transform│
└──────────────────┘                                └──────────────────┘
```

### CH-tree-qwen-md-01 · Planned repo tree (historical)

The planned repository layout at project start.

*Sites:* `docs/archive/qwen_featuer.md:172-208` — master

```
┌─────────────────┐      USB CDC (JSON lines)      ┌──────────────────┐
│   Host PC        │ ◄───────────────────────────── │   ESP32-S3       │
│                  │     BLE advertisement stream    │   Dongle         │
│   LLM analyzes   │                                │                  │
│   BLE data       │                                │   Captures BLE   │
│                  │ ──────────────────────────────► │   advertisements │
│   LLM generates  │     Lua script (text)          │                  │
│   Lua script     │     (deployed to device)       │   Executes Lua   │
│                  │                                │   filter/transform│
└──────────────────┘                                └──────────────────┘
```

### CH-tree-status-md-01 · Repo tree snapshot (historical)

The actual tree as of 2026-07-19 (proto/json/filter era).

*Sites:* `status/archive/status-2026-07-19-0200.md:196-235` — master

```
esp32_lua_llm/
├── CMakeLists.txt              ← ESP-IDF project root
├── sdkconfig.defaults          ← ESP32-S3 config
├── partitions.csv              ← Flash layout (nvs+phy+factory+littlefs)
├── build.bat / flash.bat / monitor.bat  ← tmux workflow scripts
├── qwen_featuer.md             ← Full product plan
├── tostudy.md                  ← Learning roadmap (14 topics)
├── CODE_REVIEW.md              ← Code review findings
│
├── interfaces/                 ← Public API headers
│   ├── proto_if.h              ← ADV parser (error codes -100~-199)
│   ├── json_if.h               ← JSON encoder (error codes -200~-299)
│   └── filter_if.h             ← Filter engine (error codes -300~-399)
│
├── firmware/components/        ← Module implementations
│   ├── proto/proto_adv_parse.c
│   ├── json_enc/json_encoder.c
│   └── filter/filter_engine.c
│
├── main/main.c                 ← ESP32 entry point (demo)
│
├── tests/host/                 ← Host-side unit tests (Unity)
│   ├── test_adv_parser.c       (14 tests)
│   ├── test_json_encoder.c     (11 tests)
│   ├── test_filter_engine.c    (14 tests)
│   └── test_main.c             (runner)
│
├── harness/                    ← AI development truth source
│   ├── 00-global-context/      (coding rules, git workflow, build env, overview)
│   ├── 01-features/            (13 feature docs across 4 stages)
│   ├── 02-knowledge/           (lessons learned — empty for now)
│   └── templates/              (feature template, test template)
│
├── bug_check/                  ← Bug reports and fix reports
│   ├── bug-luminous-spring.md  (30 issues found)
│   └── bug_fix_report/
│       └── fix-report-2026-07-19.md
│
└── status/                     ← Session status reports
    └── status-2026-07-19-0200.md  (this file)
```

### CH-matrix-design-md-01 · Module vs ESP-IDF built-ins (historical)

The build-vs-reuse decision matrix from the design review.

*Sites:* `docs/archive/design_patch.md:8-31` — master

*(rendered diagram — see the site link above; not embeddable as text)*

### CH-tmpl-state-md-01 · State-machine placeholder (template)

Placeholder for feature spec authors — replace with the feature's real state machine.

*Sites:* `docs/templates/feature_template.md:23-25` — master

```
 STATE_A ──event──▶ STATE_B ──event──▶ STATE_C
   ▲                                      │
   └──────────── event ──────────────────┘
```

### CH-tmpl-tree-md-01 · Test harness tree placeholder (template)

Placeholder tree for unit-test template authors.

*Sites:* `docs/templates/unit_test_template.md:164-171` — master

```
tests/harness/
├── CMakeLists.txt
├── unity/                  # Unity source (unity.c, unity.h, unity_internals.h)
├── mocks/                  # Mock/stub .c files for dependencies
│   └── mock_scan_pipeline.c
├── src/                    # Module source copied/linked for host build
│   └── cli_commands.c
└── test_cli_on_target.c    # This test file
```

### CH-toolchain-study-md-01 · MinGW vs xtensa toolchains (study note)

Same C sources, two compilers: native MinGW gcc → host test.exe vs xtensa-gcc → firmware.bin.

*Sites:* `docs/tostudy.md:511-516` — master

```
Your PC (x86_64)                    ESP32-S3 (Xtensa LX7)
┌─────────────┐                     ┌─────────────┐
│ MinGW gcc    │ ──► test.exe       │ xtensa-gcc   │ ──► firmware.bin
│ (native)     │   runs on PC       │ (cross)      │   runs on ESP32
└─────────────┘                     └─────────────┘
Same .c files, different toolchains, different outputs
```

### CH-buildio-study-md-01 · Build inputs vs outputs (study note)

What you write (CMakeLists, sdkconfig, components) vs what the build generates (bin, sdkconfig.h, partition table).

*Sites:* `docs/tostudy.md:541-547` — master

```
Your PC (x86_64)                    ESP32-S3 (Xtensa LX7)
┌─────────────┐                     ┌─────────────┐
│ MinGW gcc    │ ──► test.exe       │ xtensa-gcc   │ ──► firmware.bin
│ (native)     │   runs on PC       │ (cross)      │   runs on ESP32
└─────────────┘                     └─────────────┘
Same .c files, different toolchains, different outputs
```

