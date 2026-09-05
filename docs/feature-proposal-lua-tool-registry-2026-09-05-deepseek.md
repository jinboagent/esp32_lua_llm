# Lua Tool Registry — Result Channel (deepseek addendum)

| Field   | Value                                                            |
|---------|------------------------------------------------------------------|
| Date    | 2026-09-05                                                       |
| Status  | **DESIGN DISCUSSION** — addendum to `feature-proposal-lua-tool-registry-2026-08-29.zcode.md` |
| Author  | deepseek (this session)                                          |
| Scope   | makes the **Lua → host result channel** explicit and first-class |

## 1. What this adds

The zcode proposal already has a result path in spirit — its L3 layer is
`LUA EXEC return name({...})`. This addendum promotes that into a designed-in
element: **a Lua script (or any tool function) can return a value, and that
value is shipped back to the host.** The product owner confirmed this is
important and requires no new machinery.

## 2. It needs zero new firmware

Two existing paths already prove the Lua→host direction works, so the result
channel is the existing `LUA EXEC` return path, not new code:

- **`lua_engine_exec()`** (`interfaces/lua_if.h`, `firmware/components/lua/lua_port.c`)
  already captures a script's return value into a `result` buffer — the
  `LUA EXEC` command returns it today.
- **the `transform` hook** already returns a string that the pipeline emits to
  the host as the outgoing line (`firmware/components/ble/scan_pipeline.c`,
  the `lua_engine_call_transform` → `usb_console_send_json` path).

So a general "Lua returns → host sees it" already works in two places; the
only change is to treat it as a first-class capability rather than an
accidental side effect of two specific paths.

## 3. Value flow is bidirectional

- **host → Lua (call):** `LUA EXEC return name({...})` invokes a tool.
- **Lua → host (result):** the exec's return value is captured by
  `lua_engine_exec()`'s result buffer and shipped back as the response.

## 4. Honest limit (locked in discussion 2026-09-05)

The value is the **software return value**, not physical ground truth.
`set_heater(true)` returning `nil` means "executed", not "the relay
physically closed". Observing the physical outcome is a **read-back tool's**
job (a temperature sensor reading back, a feedback pin), not the transport's.
So the result channel is strong for *read* tools and only an ack for
*actuation* tools — which is a hardware/sensing concern, not a protocol one.

## 5. Result model (decided)

| Milestone | Result form                                                        |
|-----------|--------------------------------------------------------------------|
| M1        | string results — `LUA EXEC` returns string/number/bool via snprintf, ~256 B |
| M3        | structured results — Lua tables → JSON via firmware marshal        |

Until M3, the convention stays "tools return compact strings".
