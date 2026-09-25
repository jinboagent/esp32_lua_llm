# User guide — chat with the LLM, deploy Lua, define tools

> **Author:** zcode · 2026-09-25 · The interactive workflow with
> `host_app/assistant.py`. Feature overview: `docs/features.zcode.md`.

## 0. Setup

```bat
pip install pyserial
copy .llm_env.example .llm_env     :: then paste your API key into it
python host_app\assistant.py COM12
```

`.llm_env` is gitignored — it holds your credentials. Any OpenAI-compatible
endpoint works (OpenAI, Qwen/DashScope, DeepSeek, OpenRouter, Ollama); the
variable names are documented in `.llm_env.example`. If your shell already
exports `LLM_API_KEY`/`OPENAI_API_KEY`, those win as a group — unset them if
you want the file to apply.

No key? `python host_app\assistant.py COM12 --no-llm` gives you the session
shell (device planes, buffers, `/tools`) without any network.

## 1. Talk to the LLM about live traffic

Anything you type that doesn't start with `/` goes to the model, together with
a fresh snapshot of the advertisement and connection streams. The model always
replies in a typed envelope:

| Envelope | Meaning | What you do |
|----------|---------|-------------|
| `answer` | a plain answer to your question | read it |
| `clarify` | the model asks **you** a question | answer it in your next message |
| `lua` | the model proposes Lua code | review it, then answer the `deploy? [y/N]` gate |
| `error` | something failed (provider, device) | the session stays alive; try again |

Example session:

```
you>    /scan on
you>    which devices around me are Apple?
answer> I can see 3 Apple devices: Sensor_A (-52 dBm), ...
you>    keep only the two strongest and drop everything else
lua>    function on_adv(addr, t, rssi, name, ...) ... end
deploy? [y/N] y
tool>   deployed — SCRIPT RUN confirmed, filter active
```

Only `y`/`yes` deploys. The artifact is uploaded line-by-line through the
sandbox scan; the device can still reject it mid-upload with `-612` — that
rejection is shown to you verbatim (and to the model as feedback).

## 2. Session commands

| Command | Effect |
|---------|--------|
| `/samples [n]` | show the last n buffered advertisement lines |
| `/scan on\|off` | start/stop scanning |
| `/conn on [addr [public\|random]]` / `/conn off` / `/conn status` | connection plane control |
| `/conn target <svc-uuid> [<chr-uuid>]` | preset the auto-connect target |
| `/deploy` | re-offer the last proposed Lua artifact |
| `/tools` | list registered tool packs and their tools |
| `/tools refresh` | re-read manifests from the device (clears the registry after a device reset) |
| `/tools load <file.lua>` | load a pack from the PC (e.g. `host_app/tool_packs/demo.lua`) |
| `/tools load @<name>` | run a pack stored on the device (`PACK RUN`) and register it |
| `/tools persist <name> [autorun]` | store the last loaded pack on the device (`PACK BEGIN/END`; `autorun` = run at every boot) |
| `/history` | show the conversation so far |
| `/quit` | exit (Ctrl+C also stops script/scan/conn and exits cleanly) |

Every session is tee'd to `assistant_<timestamp>.log` in the working directory.

## 3. Define your own Lua tools (tool packs)

A pack is a Lua file where **every line is one complete statement ≤ 240 bytes**,
private globals carry a pack prefix, and every tool takes **one table argument**
and returns a string. `manifest()` advertises the API:

```lua
mypack_last = nil                       -- private: pack-prefixed

function ctof(a)                        -- ONE table arg, named fields
  return string.format("%.1f", a.c * 9 / 5 + 32)
end

function manifest()
  return '{"version":1,"name":"mypack","tools":'
      .. '[{"name":"ctof","doc":"celsius to fahrenheit",'
      .. '"args":[{"name":"c","type":"number"}],'
      .. '"returns":"string","mutating":false}]}'
end
```

Load it with `/tools load mypack.lua`, confirm, and the tool is callable — by
you through the model ("convert 21.5 C to F") and by the model itself inside
generated programs. Mark tools that touch hardware state with
`"mutating":true` and run the assistant with `--mutating-gate` to confirm
before such programs execute.

Useful extras:

- `hw.*` bindings (GPIO/ADC/kv store) are available inside packs —
  see `docs/features.zcode.md` §4 for the exact list.
- `/tools persist mypack autorun` makes the pack survive reboots and load at
  boot — the dongle then has your tools with no PC attached.
- The demo packs in `host_app/tool_packs/` are the reference examples.

## 4. Bounded autonomy — what runs without asking

| Artifact | Gate |
|----------|------|
| Lua defining `on_adv` / `transform` / `manifest` (filters, packs) | `deploy? [y/N]` — always |
| Tool programs (compose already-registered tools) | run immediately, max **3 consecutive executions per turn**, then the model must answer plainly; with `--mutating-gate` they confirm first |
| Anything the device rejects | the error goes back to the model as a hint — it retries or explains |

## 5. One-shot alternative: `llm_loop.py`

No conversation, just the loop:

```bat
python llm_loop.py COM12 capture --secs 10 --out adv.jsonl
python llm_loop.py analyze --in adv.jsonl --out filter.lua --goal "keep only Sensor_* devices"
python llm_loop.py COM12 deploy --script filter.lua
python llm_loop.py COM12 loop --secs 8 --goal "keep only my Sensor_* devices"
```

`analyze` writes the generated Lua to a file so you can review it before
deploying; `--dry-run` runs the whole mechanics with a bundled sample script
and no API key.

## 6. Assistant options

| Option | Effect |
|--------|--------|
| `--no-llm` | session shell only, no network |
| `--system-file PATH` | replace the built-in system prompt |
| `--system-extra PATH` | append your own steering text |
| `--mutating-gate` | confirm before executing programs that use mutating tools |
| `--native-tools` | expose registered tools to the model as native function-calling tools (on models that support it) instead of prompt-described conventions |
