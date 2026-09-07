# FEATURES — index of features included in this baseline

> **Author:** zcode · 2026-09-03
> **Baseline:** `master` · **Head at indexing:** `e287261` (2026-09-03)
>
> Maintained per `docs/workflow-feature-branches-2026-09-03.zcode.md`:
> every squash merge adds or updates one entry. Entries point at the
> authoritative spec in `harness/01-features/` — this file is an index,
> not a spec.

## Release tags

- `v1.1.0` @ `71e0091` (2026-08-29) — stages 0–5 + F2.4 conn plane +
  the 2026-08-29 test-hardening campaign.

## Included features

### Stage 0 — base

| Feature | What it adds | Landed |
|---|---|---|
| USB CDC console (`feature_usb_cdc_console.md`) | USB serial console: line-buffered command input, JSON response output — the dongle's only host channel | stage squash, pre-v1.1.0 |
| LittleFS storage (`feature_littlefs_storage.md`) | Persistent script slot `/littlefs/script.lua` (8 KB single slot), storage error range -7xx | stage squash, pre-v1.1.0 |

### Stage 1 — protocol

| Feature | What it adds | Landed |
|---|---|---|
| JSON encoder (`feature_json_encoder.md`) | Strict-JSON line encoding for adv/conn/script output; fuzz-hardened 2026-08-29 (3 defects found+fixed) | stage squash, pre-v1.1.0 |
| AD parser (`feature_adv_parser.md`) | BLE advertisement payload → structured fields (name, UUIDs, TX power, manufacturer data) | stage squash, pre-v1.1.0 |
| Filter engine (`feature_filter_engine.md`) | C-side match filters (MAC/name/UUID) ahead of the Lua hooks | stage squash, pre-v1.1.0 |

### Stage 2 — BLE core

| Feature | What it adds | Landed |
|---|---|---|
| NimBLE init (`feature_nimble_init.md`) | BLE host/stack bring-up, scan-state atomics | stage squash, pre-v1.1.0 |
| BLE scan (`feature_ble_scan.md`) | Passive advertisement scanning + dedup table | stage squash, pre-v1.1.0 |
| Scan pipeline (`feature_scan_pipeline.md`) | The data plane: BLE cb → AD parse → C filters → Lua hooks → JSON line → USB | stage squash, pre-v1.1.0 |
| **F2.4** BLE connection (`feature_ble_conn.md`) | Optional GATT-central plane: connect/notify/poll a peer, re-stream as `"src":"conn"` lines (analysis-only, bypasses Lua hooks) | `ble_connected_zai` line merged `ce3baef`; GATT data-path fix via `fix/f24-gatt-data-path` → `151f14a` |

### Stage 3 — Lua

| Feature | What it adds | Landed |
|---|---|---|
| Lua 5.4 port (`feature_lua_port.md`) | Lua on a static pool allocator, sandbox whitelist (base/string/table/math/utf8), engine lock, `LUA EXEC` result path (256 B) | stage squash, pre-v1.1.0 |
| Script management (`feature_lua_script_mgmt.md`) | `SCRIPT LOAD/END/RUN/STOP` + per-line fail-closed sandbox scan (-612), `on_adv`/`transform` hooks | stage squash, pre-v1.1.0 |

### Stage 4 — integration

| Feature | What it adds | Landed |
|---|---|---|
| CLI commands (`feature_cli_commands.md`) | Full CLI surface, strict-JSON responses, CLI state machine (-911) | stage squash, pre-v1.1.0 |
| Lua↔LLM bridge (`feature_lua_llm_bridge.md`) | F4.1/F4.2: host↔dongle script upload bridge + exec plumbing | stage squash, pre-v1.1.0 |
| Power management (`feature_power_management.md`) | F4.3: power-state commands + hw verification | stage squash, pre-v1.1.0 |

### Stage 5 — host tooling

| Feature | What it adds | Landed |
|---|---|---|
| **H5.1** `llm_loop.py` (`feature_llm_loop_tool.md`) | One-shot loop: capture → LLM writes Lua → deploy → verify; provider-pair config + self-heals | `76ab745`; convergence pass `c308496` (direct) |
| **H5.2** `host_app/run_case.py` (`feature_host_cases.md`) | Pluggable conn-plane cases + `--estimate` ground-truth check; cases: plant (notify), first_order_poll (read-only poll) | squash `293ebec` from `feature/5-host-cases`; `first_order_poll` added direct `e287261` |
| **H5.3** `host_app/assistant.py` (`feature_assistant_session.md`) | Interactive REPL: typed envelopes (answer/lua/clarify/error), human-confirmed deploys, `--system-extra/--system-file` | squash `9616339` from `feature/5-assistant` |

### Direct on master — by owner instruction

The 2026-08-29 test-hardening campaign and follow-ups were committed
directly to master at the owner's explicit instruction (no feature
branch); recorded here so the index stays complete:

- `efdf3e4..71e0091` (tagged `v1.1.0`): response-contract suite (76 CLI
  responses strict-JSON validated), deterministic LCG fuzz + boundary +
  scanner corpus (found and fixed 3 JSON-encoder defects), golden
  transcripts + whole-REPL tests with `DeviceSimSerial`, C7 conn state
  matrix + C2 peer-retry (`test_ble_conn_hw.py` 65/0), conn soak,
  `tests/hw/run_all_hw.py` one-command regression gate, optional gcov
  coverage build.
- `c308496`: llm_loop converged onto stage-5 conventions (read-timeout
  catch, thinking-flag handling, cmd-field matching).
- `e287261`: `first_order_poll` case — the dongle poll path, ground-truth
  checked (30/30).
- `51dc9c6`: BLE component thread-model README
  (`firmware/components/ble/README.md`) — the 4-task concurrency model,
  sync primitives, and adv/conn data paths.
- `4022b72`: Windows BLE peripheral capability test
  (`vendor_reference/ble_test/`) — WinRT tool proving the laptop can act
  as a BLE peripheral, a reusable peer for conn-plane tests.

## Not yet in this baseline (open branches)

| Branch | Cut from | State |
|---|---|---|
| `Lua_tool_extension_dev` | master @ `d0b73a3` (2026-09-07) | **H6.1 Lua Tool Registry M1–M4 implemented + live-verified** (pack convention + generate-and-execute; LittleFS pack persistence + boot autorun; `hw.*` device API incl. kv configure mode + chunked exec + mutating gate; native function-calling option) — Unity 147, python 144/35/10, hw 13/13 + 18/18 + gate 6/6 — unmerged, review pending; spec `harness/01-features/stage6-agent/` |
| `ble_connected` | pre-F2.4-merge | historical implementation line; content superseded by the merged F2.4 |
| `docs/stage5-refresh` | pre-`d090091` | merged via squash `d090091`; kept per policy |

Already merged and kept (revert path): `feature/5-assistant`,
`feature/5-host-cases`, `fix/f24-gatt-data-path`.
