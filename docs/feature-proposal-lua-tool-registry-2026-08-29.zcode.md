# Feature Proposal — Lua Tool Registry (LLM Tool Registration by the Device Expert)

| Field       | Value                                              |
|-------------|----------------------------------------------------|
| Date        | 2026-08-29 (revised 2026-09-05, absorbing the deepseek addendum; decision 14 — deploy modes — added 2026-09-03) |
| Branch      | none — design discussion only, nothing implemented |
| Status      | **PROPOSAL — DESIGN CONVERGED, awaiting the owner's go-ahead** (decisions 1–14 locked with the product owner — §4 table, §5.1, §8 resolutions; no open questions; NO code written) |
| Author      | **zcode (ZCode agent)**, from the architecture discussion with the product owner (2026-08-29 →) |
| Addenda     | `feature-proposal-lua-tool-registry-2026-09-05-deepseek.md` (result channel) — absorbed in §3.1/§4/§6, credited inline |
| ID          | H6.1 (stage 6 — agent platform), when promoted     |

---

## 1. The idea, in one paragraph

Lua scripts on the microcontroller stop being only "programs the LLM
writes" and become **capability declarations**: a *tool pack* is a Lua
script, written by the person who knows the hardware, that defines its
callable functions **plus a `manifest()` function returning a JSON
description of them** (names, argument types, docstrings, examples).
The host fetches the manifest and injects it into the LLM's context;
the LLM then *calls* those functions as tools at runtime. In modern
terms: **function-calling / tool registration for the LLM — but the
registry is authored in Lua on the device, and the device is its own
server.**

## 2. Why this matters (the rationale agreed in discussion)

- **Knowledge amortization.** The scarce resource in LLM + hardware is
  device knowledge (pins, registers, sequences, quirks). Today it lives
  in C or in one engineer's head, and every LLM session re-learns it by
  prompt gymnastics. A tool pack encodes it **once**; every future
  session, operator, and host inherits it as a typed, callable
  interface.
- **Self-describing devices.** Plug any controller running this pattern
  into any host and the assistant immediately knows what it can do —
  no per-device host drivers, no per-device prompts.
- **Two planes, one platform.** The existing loop is the *data plane*
  (the LLM **programs** the device: writes Lua filters for the stream).
  This feature is the *control plane* (the LLM **operates** the device:
  calls expert-registered capabilities). Together they make the dongle
  a reference implementation of an LLM-operable edge device.

## 3. Architecture (four layers)

```
L3  call & result channels   CALL: LLM emits {"type":"tool_call","name","args"};
                             host serializes args -> Lua table literal;
                             wire: LUA EXEC return name({...})   (M1)
                             RESULT: the exec's return value is captured by
                             lua_engine_exec()'s result buffer and shipped
                             back as the response — a first-class platform
                             promise, not an accident of one code path
L2  manifest transport    host fetches LUA EXEC return manifest()
                          -> JSON -> system prompt (M1, text)
L1  in-script declaration function manifest() returns JSON string;
                          the tool functions live beside it;
                          single source of truth, no drift (M1)
L0  device API for Lua    hw.gpio/adc/i2c/millis bindings, per-build
                          whitelist — where device expertise physically
                          lives (M3; none today: the sandbox opens only
                          base/string/table/math/utf8)
```

### 3.1 The result channel, made explicit (absorbed from the deepseek addendum, 2026-09-05)

Two existing paths already prove the Lua → host direction, so the
result channel costs zero firmware and is a *promise*, not new code:

- **Pull mode (the tool path):** `lua_engine_exec()` captures the
  return value into its result buffer; `LUA EXEC` returns it today.
- **Push mode (the data-plane path):** the `transform` hook returns a
  string the pipeline emits as the outgoing stream line
  (`scan_pipeline.c`). This is the data plane's own instance of the
  same capability — and the seed of a future *tool-event* mode (open
  question 7).

**The honest limit (locked with the product owner, 2026-09-05):** the
result is the **software return value, not physical ground truth**.
`set_heater(true)` returning `"ok"` means *executed*, never *the relay
physically closed* — observing the physical outcome is a read-back
tool's job (a sensor reading, a feedback pin), not the transport's.
Consequences for the convention:

- Every tool returns a string, **including actuation acks** (never rely
  on nil) — the ack is always explicit.
- Each tool's `doc` states whether its return is a **measurement** or
  an **ack**; M2 validation can promote this to a
  `returns: "value" | "ack"` manifest field so the LLM reasons about
  physical effects correctly.

**Trust model (revised by decision 10, 2026-09-05):** *humans register
and set intent; the LLM executes autonomously; the host script is the
safety layer.* Registration (uploading a tool pack) remains an explicit
human act. Call-time approval is NOT required: the LLM composes and
executes tool programs on its own, and humans participate at the intent
level — stating or revising the controller's purpose, answering the
LLM's `clarify` questions, or volunteering information. The assistant
(the Python layer on the host) is **the middle layer between LLM and
microcontroller**: it is the only party that talks to the device, and
it owns policy — manifest injection, execution, result relay, clarify
relay, and loop guards. The device sandbox, compile check, and
instruction budget remain the hard floor under everything. A
mutating-only gate may return as a HOST-LAYER POLICY OPTION when M3
introduces real hardware (the host holds the manifest, so it can
statically detect mutating tool names in generated code).

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

### 3.2 The two call flows: push (data plane) vs pull (control plane)

```
PUSH — the device initiates, machine-paced (exists today):
  BLE ads ─► scan_pipeline(C) ─┬─ on_adv(...)   ◄─ device calls your Lua
                               ├─ transform(..) ◄─ (push-mode result channel)
                               ▼
                       JSON stream ─► USB ─► host ─► LLM reads snapshots
  (LLM's roles: AUTHOR-time — it wrote the hook once; READ-time — sees
   the stream; never present at the event itself)

PULL — the LLM initiates, autonomous in-session (the tool registry, M1):
  user ─► LLM (system prompt carries the manifest)
        ─► generates Lua composition calling registered tools
        ─► host layer executes it (LUA EXEC; tee = audit)
        ─► host: LUA EXEC return read_temp({unit="C"}) ...
        ─► device Lua state (tools wait passively)
        ─► result string returns (pull-mode result channel, §3.1)
        ─► LLM answers / composes the next call
```

One session composes both: pull to *decide* (query tools), push to
*act continuously* (author a hook implementing the decision).

## 4. Decisions locked with the product owner (2026-08-29)

| # | Question | Decision | Reason |
|---|----------|----------|--------|
| 1 | v1 scope | **Pure-Lua tools first**; no `hw.*` in M1 | prove the loop with zero hardware risk; budget semantics for blocking calls arrive with M3 |
| 2 | Call transport | **Reuse `LUA EXEC`** on the wire | zero firmware changes in M1; the assistant serializes structured calls to safe Lua literals so the LLM still thinks in structured calls |
| 3 | Manifest → LLM | **Text manifest in the system prompt** | model-agnostic (works on any OpenAI-compatible endpoint, incl. the current MaaS one); native function-calling is a later option |
| 4 | Trust boundary | **Humans register, LLM only calls** (call-time gating amended by decision 10: autonomous execution, human-at-intent) | registration is the privileged act — that half stands |
| 5 | M1 shape | **Host-only, zero firmware** | the convention + assistant support prove everything; firmware `TOOLS LIST`/`TOOL CALL` are M2/M3 |
| 14 | How LLM-produced Lua persists after power-up — the 2026-09-03 audit's open question #1 (decisions 6–13 are the §8 resolutions) | **Deploy-intent flag in the host layer** selects the persistence class: `run` / `configure` / `resident` (§5.1); M1 ships `run` (session/RAM) + `resident`-via-host | what must survive a power cycle is the *effect* or the *script*, chosen per deployment; the host — the middle layer (decision 10) — owns that policy; device-owned parameter storage (M3) and boot-time `autorun` (M2) are reserved seams |
| 6 | Result channel (2026-09-05, from the deepseek addendum) | **First-class pull-mode result channel** on the existing `LUA EXEC` return path; **honest limit locked**: a return value is software truth (measurement or ack), never physical ground truth — read-back tools observe physical outcomes | zero new machinery; makes tool authoring a trustworthy contract; the LLM must reason about ack-vs-measurement correctly |
| 7 | L3 call style (2026-09-05) | **Generate-and-execute**: the LLM writes executable Lua composing the registered tools (existing `lua` envelope + `LUA EXEC`/bridge; confirm dropped by decision 10); structured `tool_call` envelopes demoted to an optional later mode | reuses the entire existing generate->confirm->execute->result loop; the product owner's instinct confirmed it is "what we have right now" — composition (loops/conditionals across tools) for free; only cost is per-program instead of per-call gating of mutating tools |
| 8 | Multi-script (2026-09-05) | **Multiple Lua scripts are a feature NOW**: multiple named tool packs stored and active side by side in the shared Lua state + the one filter script; cross-pack name collisions rejected host-side at load. Hook *chaining* (several scripts owning on_adv/transform) is explicitly out of scope | the product owner: "there should be multiple lua script right now, it is a feature"; a tool pack must not evict the filter script |
| 9 | Manifest validation location (2026-09-05) | **Host-side** (JSON string/file validated by the host app); on-device `TOOLS LIST`/validation dropped from the critical path | View A (product, not convention): the manifest is inert data — the functions are what run, and those are already sandbox-scanned + compile-checked on-device |
| 11 | Tools vs CLI state machine (2026-09-05) | **Permanently outside** — tool execution valid while scanning or connected; engine lock serializes | autonomous execution needs no state-machine gymnastics; matches the A1 matrix keeping script_running for the filter role only |
| 13 | Activation & persistence (2026-09-05) | **Session-scoped now (option A)**; boot-time activation deferred as application-dependent with a reserved manifest placeholder `"autorun": false` (ignored in M1, honored by M2 firmware if an application needs it) | defer to the last responsible moment: the decision needs application knowledge that doesn't exist yet; the placeholder keeps the option open at zero cost |
| 12 | Push-mode events (2026-09-05) | **Host-side pseudo-push** as an optional M1.5 experiment; device-side emit() deferred to M3+ (fire-while-host-away or fast-cadence need) | gets the event UX with zero firmware; experience decides whether emit() earns its firmware cost |
| 10 | LLM call discipline (2026-09-05) | **Autonomous execution; human-at-intent.** No per-call/per-program approval; humans state the controller's purpose, answer `clarify`, and volunteer info. The host Python script is the middle layer between LLM and MCU and owns policy/safety. Amends decision 4's call-time gating (registration stays human-only); mutating-gate becomes a host-policy option at M3 | the LLM operates as an agent, not a button-presser; safety belongs to the deterministic host layer + device sandbox, not to constant human approval — in M1 nothing physical can happen anyway |

## 5. The M1 convention (proposed — what a tool pack would be)

```lua
-- every line must be a complete statement (fits the LUA EXEC line
-- path, <= ~240 bytes); the pack defines manifest() plus its tools
function manifest()
  return [[{"version":1,"name":"demo","tools":[
    {"name":"mean","doc":"Arithmetic mean of a list",
     "args":[{"name":"numbers","type":"array","item_type":"number"}],
     "returns":"string","mutating":false,
     "example":{"args":{"numbers":[3,5,10]},"result":"6.0"}}]}]]
  -- top level may carry "autorun": false — a RESERVED PLACEHOLDER for
  -- boot-time activation (decision 13): M1 hosts ignore it; M2
  -- firmware may honor it. Declaring the field name now guarantees no
  -- schema break when the deferred decision lands.
end
function mean(a) ... end   -- takes ONE table argument; returns a string
```

- Activation: `/tools load <file.lua>` uploads via the normal
  `SCRIPT LOAD/END` bridge (persisted + compile-checked + sandbox
  scanned — the existing trust chain), then defines the pack's globals
  in the live engine state by `LUA EXEC`-ing each line (globals persist
  in the shared Lua state; no scanning, no CLI state change). Multiple
  packs may be active side by side (decision 8); the host rejects
  cross-pack name collisions at load time.
- Fetch: `LUA EXEC return manifest()` -> strict-JSON validated
  HOST-side (decision 9: version, unique names across all active packs,
  arg shapes; identifiers must be safe to splice into an exec line).
  The device stays dumb about manifests.
- **Use (L3, revised 2026-09-05 per the product owner — generate-and-
  execute):** the manifest is injected into the system prompt teaching
  the LLM the callable functions; the LLM then REPLIES WITH EXECUTABLE
  LUA that composes them (the assistant's existing `lua` envelope),
  e.g. `local t=read_temp({unit="C"}); if tonumber(t)>30 then
  heater_on({}) end; return t`. The host layer executes it directly
  (decision 10: autonomous execution — humans act at intent level and
  via clarify, not per-program approval) — `LUA EXEC <code>` for one-liners (~245 B, result
  returned), the bridge path for longer line-complete programs. The
  tee'd code IS the audit trail. This reuses the entire existing
  generate -> confirm -> execute -> result loop; the structured
  `tool_call` envelope + args serializer of the original plan become
  an OPTIONAL later mode (its only unique value: per-call gating of
  mutating tools instead of per-program gating).

### 5.1 Deploy modes — what survives a power cycle (decision 14, 2026-09-03)

The owner's framing of the storage question: not "where does the
script live" but **what must survive power-up** — and the choice is a
**flag in the host Python layer**, because deploy policy lives in the
middle layer (decision 10). Three intents cover everything the LLM
produces:

| Mode | Intent | What survives | Mechanics today | Reserved seam |
|------|--------|---------------|-----------------|---------------|
| `run` *(default)* | execute now — tool calls, one-shot compositions | nothing, by design | `LUA EXEC` defines globals in the live Lua state (RAM); cleared by reboot / `LUA INIT` | — |
| `configure` | set parameters; afterwards the MCU owns them | the **effect** (the parameters), not the script | M1: a parameter is a Lua global → session-only; real device-owned persistence arrives with the L0 device API | M3: key-value store behind `hw.*` — literally "the microcontroller takes care of these parameters" |
| `resident` | execute forever — filters/hooks, watchdogs | the **script** + its running role | `SCRIPT LOAD/END` persists to the single flash slot (compile-check + sandbox scan); the host re-issues scan + `SCRIPT RUN` after each boot; a new upload evicts the slot's previous occupant | M2: manifest `"autorun": true` honored by firmware at boot (decision 13's placeholder) |

Locked consequences:

- **M1 scope settled (the 2026-09-03 audit's question #1): RAM-multi /
  flash-single ACCEPTED.** Several packs may be active in one session
  (their globals coexist in the live Lua state); only the last
  `SCRIPT LOAD` persists in flash; the host's pack files are the
  source of truth and re-activate each session.
- The flag is host-side only in M1 (a deploy option on the assistant /
  llm_loop paths); the manifest's `"autorun"` placeholder is the
  device-side seam the same flag graduates into at M2 — one concept,
  two homes, no format change.
- `configure` is honest about M1: a parameter set by a pure-Lua tool
  lives exactly as long as the session; anything that must outlive
  power today has to be a `resident` script instead.

## 6. Risks and mitigations

| Risk | Mitigation |
|------|-----------|
| Manifest quality is the ceiling (bad docs -> bad calls) | `example` (few-shot) per tool required in the format; doc conventions in the demo pack |
| Side-effect tools + LLM | `mutating` flag in the manifest; autonomous execution per decision 10 with the host layer as policy owner — a mutating-only gate is a host-policy option when M3 hardware lands; audit via the tee transcript |
| **LLM mistakes an ack for physical confirmation** (the §3.1 honest limit: `set_heater(true)` -> `"ok"` does not mean the relay closed) | every tool's doc states measurement-vs-ack; `returns: "value"\|"ack"` manifest field from M2; actuation results phrased as acks (`"ok: heater on"`); read-back tools for physical truth |
| **Nil / missing returns** on the exec path | convention: every tool returns a string, including actuation acks — never rely on nil |
| Args/results size (256 B in / ~256 B out) | literal length caps; results trimmed with a marker; convention keeps tools chunky, not chatty |
| Injection through spliced exec lines | `lua_literal` escapes everything; tool names validated as identifiers; the sandbox + engine budget remain the final gate |
| Schema drift | manifest carries `version:1` from day one |

## 7. Roadmap (proposed)

- **M1 (host-only):** the convention + a demo pack + assistant support
  (`/tools`, `/tools refresh`, `/tools load`, `tool_call` envelope,
  mutating gate). Zero firmware changes.
- **M2 (revised 2026-09-05):** firmware multi-script storage — named
  packs in LittleFS (the storage layer already supports it; only the
  script manager hardcodes one path) + boot-time auto-activation of
  persisted packs (resolves open question 2). Host-side manifest
  validation is already in M1; on-device validation is OFF the
  critical path (decision 9).
- **M3:** scan-independent execution of longer generated programs
  (chunked exec or a bridge exec that does not require `script_running`),
  minimal `hw.*` binding layer (gpio/adc/millis) behind a build flag,
  per-tool wall-clock budgets for blocking hardware.
- **M4:** native function-calling option (manifest -> `tools` array),
  optional structured `tool_call` mode (per-call mutating gates),
  GATT-plane exposure, hook chaining across scripts.

## 8. Open design questions (under discussion)

1. ~~**Single-script slot vs packs-that-filter.**~~ **RESOLVED
   2026-09-05 (decision 8):** multiple named tool packs + the one
   filter script, side by side; packs may not own hooks (hook chaining
   deferred to M4); host rejects cross-pack name collisions.
2. ~~**Activation & persistence**~~ **RESOLVED 2026-09-05 (decision 13):
   session-scoped now + a reserved placeholder for the deferred choice.
   Examples retained below.**
   `SCRIPT LOAD/END` persists and compile-checks but does NOT execute;
   `SCRIPT RUN` requires scanning and enters `script_running` state.
   M1 activation (host `LUA EXEC`s the pack line-by-line) is RAM-only.
   The options as timelines:
   - **A. session-scoped (M1, zero firmware):** /tools load per
     session (~5 s); reboot wipes tools; DIFFERENT PC without the pack
     file = stuck (flash holds the pack, no command runs or reads it
     back) — the portability hole.
   - **B. boot-time auto-activation (M2 proposal, firmware):** manifest
     "autorun": true -> firmware executes persisted packs at boot ->
     tools alive before any host connects; any PC, file or no file.
     Pairs with decision 8: M2 multi-script + autorun = "the device
     carries its own tools".
   - **C. SCRIPT DUMP (alternative):** firmware command reading the
     stored script back over USB so any host can re-activate it;
     portability without boot semantics; costs a command.
   - Rejected: SCRIPT RUN (needs scanning on, enters script_running,
     blocks CONN — the Q4 entanglement tools must avoid).
   RESOLVED: A now (the simple framework, zero firmware); boot-time
   activation is APPLICATION-DEPENDENT and deferred — the manifest
   reserves an optional "autorun": false field as the placeholder
   (M1 hosts ignore it; an application that needs it gets it via M2
   firmware honoring the field — no schema or consumer change).
   **Extended 2026-09-03 (decision 14, §5.1):** the owner reframed
   persistence as deploy INTENT — a host-side flag selects `run` /
   `configure` / `resident`. RAM-multi/flash-single is accepted M1
   scope (host pack files = the source of truth); option C
   (`SCRIPT DUMP`) stays unnecessary while the host owns the files.
3. **Result model.** ~~under discussion~~ **RESOLVED by agreement**
   (zcode §8.3 ≡ deepseek addendum §5, confirmed 2026-09-05): M1
   string results via the `LUA EXEC` return path; structured Lua
   table → JSON results with M3's firmware marshal. Convention until
   then: "tools return compact strings; every tool returns a string,
   including actuation acks" (§3.1).
4. ~~**Coexistence with the state machine.**~~ **RESOLVED 2026-09-05
   (decision 11):** tools live permanently OUTSIDE the CLI state
   machine — execution works while scanning or connected, serialized
   by the engine lock. Autonomous execution (decision 10) depends on
   this: the agent acts mid-scan without state-machine transitions.
5. ~~**LLM call discipline.**~~ **RESOLVED 2026-09-05 (decision 10):
   autonomous execution with human-at-intent.** Humans are not
   involved in the calls; they set/revise the controller's purpose,
   answer `clarify` questions, and volunteer information. The host
   script is the middle layer between LLM and microcontroller (the
   only party talking to the device; owns policy, manifest injection,
   result/clarify relay). Technical loop guards remain: cap consecutive
   program executions per user turn against runaway generate-execute
   loops; a mutating-only gate returns as a host-policy OPTION at M3
   when hardware makes actuation physically real.
6. ~~**Is the dongle the product, or is the CONVENTION the product?**~~
   **RESOLVED 2026-09-05: View A — the product.** The convention/API-
   ization is explicitly deferred ("i need more time and experience to
   understand how to make it API" — product owner). The manifest format
   stays an internal detail; revisit when external adoption becomes a
   goal.
7. ~~**Push-mode tool results**~~ **RESOLVED 2026-09-05 (decision 12): ** host-side
   pseudo-push becomes an optional M1.5 experiment (host watcher polls
   tools on a timer, injects synthetic events; zero firmware); the
   device-side emit() binding is DEFERRED until events must fire while
   the host is away or faster than USB round-trips — revisit at M3+.
   Advantages and analysis retained below for that revisit:** Pull mode covers request/response; push events would
   let the DEVICE initiate. Advantages: (a) alarm-style monitoring
   without polling — the watching costs microseconds on-device and the
   LLM pays per event, not per check; (b) events outlive the turn —
   they land on the USB stream where the rolling buffer, tee, and next
   LLM snapshot already capture them (`src:"tool"` slots beside adv and
   conn in the host's line classification); (c) they complete the
   honest-limit story — physical feedback is asynchronous, and a
   push-mode read-back subscription is how an ack eventually meets
   proof; (d) SCADA-style operator UX ("watch temp, tell me if > 30").
   Costs: a caller-less Lua scheduling context for watchdogs that fire
   with nobody asking (today's push only fires at pipeline moments),
   rate limits against USB flooding, and interrupt semantics in the
   assistant UX. **Middle path identified (zero firmware): host-side
   pseudo-push — a host watcher pulls tools on a timer and injects
   synthetic events into the session; device-side `emit()` becomes
   worth building only when events must fire while the host is away.**
   Use cases: threshold alarms, read-back subscriptions, long-running
   tool progress, live diagnostics.

## 9. Post-implementation pointers (empty until built)

Spec promotion: `harness/01-features/stage6-agent/feature_tool_registry.md`;
process report + live transcripts: `harness/02-knowledge/` — all TBD
with the first implemented milestone.
