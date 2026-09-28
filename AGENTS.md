# AGENTS.md — ESP32-S3 BLE Sniffer Dongle

> **Author:** zcode · 2026-09-07 · Workspace instructions for ZCode agents.
> Keep in sync with `CLAUDE.md` (same project, other agent readers). Full specs
> live in `harness/`; `docs/quickstart.md` is the 10-minute path.

## What this repo is

ESP32-S3 passive BLE advertisement scanner firmware → AD parse → C filters →
optional Lua hooks → strict-JSON lines over USB CDC → self-contained Python
host tools where an LLM analyzes the stream and writes Lua deployed back over
`SCRIPT LOAD`. Optional GATT-central conn plane (F2.4) re-streams peer data as
`"src":"conn"` lines — analysis-only, deliberately bypassing the Lua hooks.

Three stage-5 host tools: `llm_loop.py` (batch capture→LLM→deploy→verify),
`host_app/run_case.py` (pluggable data-generator cases, `--estimate` checks
LLM against ground truth), `host_app/assistant.py` (interactive session, typed
`answer|lua|clarify|error` envelopes, human-confirmed deploys). H6.1 (branch)
adds Lua tool packs: `manifest()` convention, generate-and-execute, `PACK`
CLI + LittleFS persistence + boot autorun, `hw.*` device bindings, `LUA
BEGIN/END` chunk exec, `--mutating-gate`, `--native-tools`. Tool functions
take ONE table arg with named fields.

## Layout

| Path | Purpose |
|------|---------|
| `interfaces/*_if.h` | Public C APIs — the only cross-module surface |
| `firmware/components/` | Implementation modules (usb, ble, proto, json_enc, filter, lua, cli, bridge, power, storage) |
| `main/main.c` | Init chain + USB command loop |
| `host_app/` + `llm_loop.py` | Stage-5 host tooling (self-contained scripts) |
| `tests/host/`, `tests/hw/` | Unity C + python unit suites; pyserial/WinRT hardware suites |
| `harness/00-global-context/` | Product spec, coding rules, build env, git workflow, publishing/privacy + documentation rules |
| `harness/01-features/` | Per-feature specs (`stage5-host/`, `stage6-agent/`) |
| `harness/02-knowledge/` | Process reports + run-transcript evidence (the WHY behind commits) |
| `docs/` | Proposals, workflow policy, commit-message standard |
| `status/LATEST.md`, `BRANCH.zcode.md`, `FEATURES.zcode.md` | Current state, active branch manifest, merged-feature index |
| `bug_check/README.md` | Consolidated bug tracking |

## Build & test (Windows host)

```bat
:: Firmware (cmd/PowerShell)
scripts\build.bat && scripts\flash.bat
```

- **Git Bash gotcha:** `idf_cmd_init.bat`/`build.bat` break there. Use
  `cmd //c "set MSYSTEM=&& set IDF_PATH=C:\Espressif\frameworks\esp-idf-v5.1&& ..."`
  with the full tool PATH — the exact working line is in `CLAUDE.md` →
  "Build & Test". Append ` -p COM12 flash` to flash. ESP-IDF v5.1 under `C:\Espressif`.
- C host tests (Unity; fresh dir, mingw + cmake on PATH):
  `cmake -S tests/host -B /tmp/hostbuild -G Ninja && cmake --build /tmp/hostbuild && /tmp/hostbuild/test_runner.exe`
  — includes the response contract (every CLI response strict-JSON validated) + fuzz.
- Python unit tests: `python tests/host/test_assistant.py`,
  `python tests/host/test_run_case.py`.
- Hardware regression gate (dongle on COM12): `python tests/hw/run_all_hw.py`;
  individual suites `test_ble_conn_hw.py`, `test_pack_hw.py`, `test_hwio_hw.py`,
  `test_ble_lua_hw.py`, `test_bridge_hw.py`, `test_power_hw.py`,
  `soak_conn.py --secs 3600 --reconnect-every 300`.
- Test conventions/evidence rules: `tests/README.md`.

## Rules that matter for edits

- **Reuse before building**: check ESP-IDF built-ins and the component registry first.
- **malloc/free**: allowed for libraries; application logic uses static buffers
  (Lua runs on a static pool allocator).
- **Error codes**: module-specific ranges in `interfaces/*_if.h` (CLI -9xx,
  bridge -6xx/-8xx, storage -7xx, BLE -4xx, pipeline -8xx).
- **Lua sandbox**: whitelist libs only (base/string/table/math/utf8); uploads
  scanned **per line, fail-closed** for forbidden tokens (`os.` `io.` `debug.`
  `require`) → -612, fired mid-upload before `SCRIPT END`.
- **Concurrency**: `lua_State` behind `lua_engine_lock/unlock`; dedup table by
  spinlock; scan state atomic.
- **Serial port (N3)**: closing COM12 resets the chip — keep the port open for
  the whole session; the final close at orderly exit is the accepted reset.
  `CONN STOP` returning ok means "terminate issued", not "state is off" — wait
  for `CONN STATUS state:"off"` before the next conn command.
- **Host tools are self-contained by design** (copied helpers, no
  cross-imports); shared *conventions*, not shared code.
- **ASCII charts are registered** (`docs/chart-registry.zcode.md`): each
  diagram has a `chart-id:` marker + a tracking row (hash + rev). After
  changing a chart, update its catalog snapshot + row and run
  `python scripts/check_charts.py` (must stay clean).
- **LLM credentials** (`.llm_env`, gitignored — never commit): env vars win as
  a UNIT; tools self-heal 401 → file fallback and Aliyun `/api/v1` 404 →
  `/compatible-mode/v1`; `QWEN_ENABLE_THINKING` honored (27B thinks >180s on
  script prompts otherwise).

## Git workflow (strict)

- **Never commit directly to master.** One feature per branch, cut from a
  registered baseline; every feature branch carries a root `BRANCH.zcode.md`
  manifest; squash-merge, then update the root `FEATURES.zcode.md` index and
  keep the branch. Policy: `docs/workflow-feature-branches-2026-09-03.zcode.md`.
- Every significant session leaves a process report + evidence in
  `harness/02-knowledge/`.
- Commit message standard: `docs/commit-message-standard-2026-08-16.md`.

## Doc conventions

- New markdown authored by an agent gets an author-tagged filename
  (`*.zcode.md` for ZCode, `*-deepseek.md` for DeepSeek) plus an author line.
  Never rename or delete committed docs.
- Before changing sensitive areas, read first: `harness/00-global-context/`
  (coding rules, build env, git workflow, publishing/privacy rules,
  documentation/language rules), the matching spec under
  `harness/01-features/`, and `status/LATEST.md` + `BRANCH.zcode.md` for
  current state (e.g. master fast-forwarded to the H6.1 tip on 2026-09-28;
  the public snapshot lives on the orphan `release` branch).
- Repo docs, comments and public-facing images are English-only, and
  nothing that ships may carry credentials or personal info. Details:
  `harness/00-global-context/documentation_rules.zcode.md` +
  `harness/00-global-context/publishing_privacy.zcode.md`.
