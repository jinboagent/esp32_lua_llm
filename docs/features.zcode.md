# Feature guide — what this dongle can do

> **Author:** zcode · 2026-09-25 · Distilled for users from the project's feature
> specifications. For hands-on paths see `docs/quickstart.md` (10 minutes) and
> `docs/user-guide.zcode.md` (the assistant & tools); for the end-to-end demo see
> `docs/demo-first-order.zcode.md`.

The dongle is a USB BLE instrument with three jobs: **watch** the radio, **run
your logic** (C filters + sandboxed Lua), and **talk to an LLM** through the
host tools. Everything below is live on the device; commands go over the
USB-Serial/JTAG console (default COM12 @ 115200), answers come back as one
strict-JSON line per command.

## 1. Passive scanning & capture

- Continuous passive discovery of BLE advertisements — no connection needed.
- Duplicate suppression (FNV-1a hash, ~1 s window) so the stream stays readable.
- Every advertisement becomes one JSON line: `ts`, `addr`, `type`, `rssi`,
  `name`, `uuids`, `manu`, `tx_power`, `flags`.
- `SCAN START` / `SCAN STOP` / `SCAN INTERVAL <ms>` (10..10000).

## 2. C-side filtering

- Up to 16 rules: `FILTER ADD NAME <pat>` (wildcards), `UUID <hex>`,
  `RSSI <dBm>`, `MAC <addr>`.
- AND between rule types, OR within a type; empty rule set passes everything.
- `FILTER LIST` / `FILTER CLEAR` (IDLE state only).

## 3. On-device Lua scripting

- Lua 5.4 on a 128 KB static pool — no heap fragmentation, ever.
- Two pipeline hooks: `on_adv(...) -> bool` (keep/drop) and
  `transform(addr, json) -> string` (rewrite the outgoing line).
- Upload with `SCRIPT LOAD` … lines … `SCRIPT END` (or the hex-chunk variant
  `SCRIPT BEGIN/CHUNK/END`), run with `SCRIPT RUN`, stop with `SCRIPT STOP`.
- One-liners any time: `LUA EXEC <code>`; multi-line chunks (4 KB, locals
  persist): `LUA BEGIN` … `LUA END`.
- **Sandbox**: whitelisted libraries only (base/string/table/math/utf8);
  uploads are scanned per line and rejected with `-612` for `os.`, `io.`,
  `debug.`, `require` and friends — the device, not the host, has the final word.

## 4. The Lua tool registry — the dongle writes its own tools

- A **tool pack** is a plain Lua file whose tools you can call from the host
  (and the LLM can compose into programs). Every tool takes ONE table argument
  with named fields and returns a string.
- A `manifest()` function inside the pack describes each tool (name, doc, typed
  args, returns, mutating flag, example) so the host — and the LLM — know the
  API without reading the code.
- Ships with two example packs: `host_app/tool_packs/demo.lua`
  (`mean`, `temp_convert`, `bench_reset`) and `hwio.lua` (6 hardware tools).
- **Persistence & autorun**: `PACK BEGIN <name> [autorun]` … `PACK END` stores a
  pack in flash (LittleFS); packs marked `autorun` execute on every boot, so
  your tools are alive without a host attached. `PACK RUN/DEL/LIST/AUTORUN`
  manage them (max 8 packs, 8 KB each, names ≤ 24 chars).
- **`hw.*` device bindings** (build flag `CONFIG_LUA_HW_BINDINGS`, default on):

  | Binding | Meaning |
  |---------|---------|
  | `hw.millis()` | ms since boot |
  | `hw.gpio_write(pin, 0\|1)` | drive a pin (whitelisted pins only) |
  | `hw.gpio_read(pin)` | read a pin back |
  | `hw.adc_read(pin)` | raw 12-bit ADC1 sample (GPIO 1..10) |
  | `hw.kv_set(key, value)` / `hw.kv_get(key)` | small persistent key-value store — values survive reboots even though scripts don't |

## 5. GATT connection plane (optional, build flag `CONFIG_BLE_CONN_ENABLED`)

- Connect to ONE peer by advertised service UUID (`CONN TARGET` + `CONN START`)
  or directly by address; subscribe to notifications/indications.
- If the peer offers no notification permission, the dongle falls back to
  polling reads (`CONN INTERVAL <ms>`, 100..10000).
- Peer data re-streams as `"src":"conn"` JSON lines on the same USB stream.
- `CONN STATUS` reports state/addr/mode/counters; `CONN STOP` disconnects.

## 6. Power management

- Automatic light sleep when idle, wake-on-USB-command.
- `POWER SLEEP ON/OFF`, `POWER STATUS` (policy, state, current estimate).
- PM locks keep the radio and console awake while scanning or connected.

## 7. Host tools — the LLM side

Three self-contained Python programs (any OpenAI-compatible endpoint: OpenAI,
Qwen/DashScope, DeepSeek, OpenRouter, local Ollama):

| Tool | What it does |
|------|--------------|
| `llm_loop.py` | one-shot: capture → LLM writes a Lua filter → deploy → verify. `--dry-run` needs no key |
| `host_app/run_case.py` | application cases with a PC-simulated BLE sensor; `--estimate` has the LLM answer a question about the data and grades it against ground truth |
| `host_app/assistant.py` | interactive chat over the live traffic: ask anything, the model answers, asks you back (`clarify`), or proposes Lua behind a `deploy? [y/N]` gate; `/tools` manages the tool registry |

See `docs/user-guide.zcode.md` for the assistant workflow and
`docs/demo-first-order.zcode.md` for the first-order estimation demo.

## 8. The safety model (why you can let an LLM touch this)

- **Sandbox on device**: per-line fail-closed upload scan, whitelist libraries → `-612`.
- **Human gates**: every script/hook deploy asks `deploy? [y/N]`; the
  `--mutating-gate` option extends the confirmation to tool programs that use
  mutating tools.
- **Bounded autonomy**: generated tool programs run at most 3 consecutive times
  per turn, then the model must answer in plain text; device errors are fed
  back as corrective hints.
- **State guards**: IDLE / SCANNING / SCRIPT_RUNNING enforced with `-911`;
  `Ctrl+C` (0x03, no Enter) interrupts anything — upload, script, connection, scan.
- **Typed errors**: each module owns a 100-wide negative range (ADV -1xx … CLI
  -9xx), so failures are machine-parseable, not prose.

## Feature flags

| Flag | Effect |
|------|--------|
| `CONFIG_BLE_CONN_ENABLED` | compiles the GATT-client plane; off = pure sniffer |
| `CONFIG_LUA_HW_BINDINGS` | exposes `hw.*` to Lua (default on) |
| `CONFIG_BT_NIMBLE_MAX_CONNECTIONS=1` | one connection by design (`sdkconfig.defaults`) |
