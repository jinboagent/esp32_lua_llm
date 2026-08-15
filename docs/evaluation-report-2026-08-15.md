# Project Evaluation — Architecture, Sandbox, Third-Party Use, and Feature Requirements

**Date:** 2026-08-15
**Scope:** read-only evaluation of firmware v1.0.0 (commit `844f62c`) against
`harness/` feature specs, plus architecture / library-reuse assessment.
**Verification basis:** source inspection, `tests/host/` (76 cases),
`status/LATEST.md` (host 76/76 · `test_ble_lua_hw.py` 45/45 ·
`test_ble_peer_hw.py` 11/11 · `test_bridge_hw.py` 32/32 · `test_power_hw.py`
14/14 · 2 h soak closed 2026-08-12).

---

## 1. Executive Summary

**The architecture is sound and, for its scope, genuinely well-engineered.**
All 13 harness features are functionally implemented and hardware-verified;
most ship as supersets of their specs. The evaluation found **no unmet
functional requirement**, three **documented/accepted scope deviations**
(USB suspend power, bridge abort semantics, library-reuse policy), a set of
**spec-vs-implementation drift** details that were never written back into the
specs, and **one genuinely unsound component**: the hand-rolled Lua pool
allocator, which contains a latent `uint16_t` offset-truncation bug and does
no free-block coalescing.

| Area | Verdict |
|------|---------|
| Module decomposition & data flow | Sound |
| Concurrency model | Sound (one lock-coupling wart on the hot path) |
| Lua sandbox security design | Sound, textbook defense-in-depth |
| Lua pool allocator | **Unsound as-written** (16-bit offsets vs 128 KB pool; no coalescing) |
| Feature requirements (13 features) | Met or exceeded; see §5 |
| Third-party library usage | Correct foundation; pool allocator is the one place reuse would clearly help |
| Spec/docs hygiene | Weakest area: stale policy, stale test-file refs, one-sided spec updates |

---

## 2. Architecture Assessment

### 2.1 What is strong

- **Clean layering.** Ten modules (`usb`, `storage`, `ble`, `proto`,
  `json_enc`, `filter`, `lua`, `cli`, `bridge`, `power`) as ESP-IDF
  components under `firmware/components/`, with the *only* cross-module
  surface being the headers in `interfaces/`. This is what makes host-side
  unit testing possible.
- **Sane data flow and backpressure.** NimBLE observer → FNV-1a dedup
  (spinlock) → FreeRTOS queue (depth 32; overflow drops and counts rather
  than blocking the radio) → pipeline task (pinned core 0) → JSON encode →
  mutex-serialized USB TX. Drop-and-count is the correct policy for
  telemetry.
- **Zero dynamic allocation outside the Lua pool** — stated and enforced in
  module headers; right default for an always-on device.
- **State machine CLI** (IDLE / SCANNING / SCRIPT_RUNNING) with `-911`
  guards; Ctrl+C always available, even mid-upload and mid-flood.
- **Testing/process discipline** far above average: three tiers (Unity host
  tests under `-Wall -Wextra -Werror`, hardware-in-the-loop Python suites
  totaling 102 checks, soak tests), plus runtime counters, stack
  high-water marks, pool stats, and the `harness/` + `bug_check/` +
  `status/` documentation culture.

### 2.2 Weaknesses (ranked)

1. **Lua pool allocator (`firmware/components/lua/lua_port.c`).**
   - `pool_block_t.size`/`.next` and `offset_of()` are `uint16_t`
     (`lua_port.c:14-29`) while `LUA_MEMORY_LIMIT` is 128 KB
     (`interfaces/lua_if.h:28`). Free/realloc churn keeps `ctx->used`
     below the soft limit while `s_heap_top` grows, so the allocator *will*
     hand out memory past offset 65535, where offsets silently truncate →
     corrupted block headers. Correctness bug, not theoretical.
   - No coalescing: free list is insert-at-head; the comment at
     `lua_port.c:95` says "sorted for future coalescing" but nothing sorts.
     Fragmentation is instrumented (H4, 2 h soak closed clean) —
     monitoring, not fixing.
   - Two accounting ledgers (`ctx->used` vs `s_pool_used`) maintained
     independently; re-synced only on the realloc path (`lua_port.c:170`),
     so they can drift on edge paths.
2. **Lock coupling on the hot path.** The pipeline takes the *Lua engine
   mutex* to run the *C filter engine* (`scan_pipeline.c:68`). The actual
   concern is CLI mutation of filter rules — the filter engine should own
   its own mutex. `lua_engine_has_func()` then acquires the global mutex
   twice more per advertisement (`scan_pipeline.c:79,97`): three lock
   round-trips per packet, and a script near its ~10 ms budget serializes
   the pipeline against a 32-deep queue in dense environments.
3. **Spec's core-separation claim not enforced.** F2.3 states "BLE runs on
   core 1; pipeline on core 0". The pipeline is pinned to core 0
   (`scan_pipeline.c:142`) but NimBLE's host task is started with default
   `nimble_port_freertos_init()` (`ble_nimble_init.c:90`) — no explicit
   pinning. Priorities still decouple them, but the documented isolation
   does not exist.
4. **Textual sandbox scan** rejects forbidden tokens even in comments
   (false positives) and is theoretically evadable by string construction.
   With `load` removed there is no payload to reach, so this is a UX cost,
   not a security hole.
5. **Thin headroom for v2.** Two 512-byte JSON buffers plus Lua hook
   machinery on the 4 KB pipeline stack — fine for 31-byte adv payloads,
   pressured by scan-response data or aggregation later.
6. **Minor.** Main loop polls `getchar()` at 10 ms cadence (pipeline blocks
   correctly, so impact is small); build artifacts and scratch logs at repo
   root; repo name (`esp32_lua_llm`) misleads about scope — no LLM/HTTP/TLS
   in firmware, by (correct) design.

---

## 3. Lua Sandbox Design (as implemented)

Six independent, defense-in-depth layers. Threat model: scripts arrive from
a host-side LLM over USB — effectively untrusted code.

| # | Layer | Where | What it does |
|---|-------|-------|--------------|
| 1 | **Whitelist library loading** | `lua_port.c:217-249` | Never calls `luaL_openlibs()`; opens only base/string/table/math/utf8. `os`, `io`, `package`, `coroutine`, `debug` never exist. Nil's out `loadfile`, `dofile`, `load`, `collectgarbage`; removes `string.dump` (kills the crafted-bytecode vector). |
| 2 | **Memory confinement** | `lua_port.c:43-173` | All Lua allocations served from a static 128 KB pool; hard byte limit returns NULL → `LUA_ERRMEM` → `-610`. Lua never touches the system heap. |
| 3 | **CPU confinement** | `lua_port.c:195-213` | `lua_sethook` `LUA_MASKCOUNT` every 1000 instructions; budget 100 fires ≈ 100 k instr ≈ ~10 ms → `luaL_error("execution timeout")` → `-614`. Re-armed around every entry point (`exec`, `on_adv`, `transform`). |
| 4 | **Concurrency confinement** | `lua_port.c:179-191` | Single `lua_State` serialized by `s_lua_mutex`; `lua_settop(L, 0)` after every call so nothing leaks across invocations. |
| 5 | **Upload gate (fail-closed token scan)** | `lua_llm_bridge.c:29-83` | Pre-buffer scan of every uploaded line: dotted tokens `os.`/`io.`/`debug.`/`package.` (substring) and bare words `dofile`/`loadfile`/`load`/`require`/`collectgarbage` (word-boundary). Tokens in strings/comments rejected too — deliberate fail-closed. |
| 6 | **Compile-before-persist + no C API exposure** | `lua_port.c:324-350`, `script_mgmt.c` | Trial compile before LittleFS write; hook arguments built entirely by C (strings/ints/tables), results copied out by value; zero firmware functions registered into Lua. |

Layering logic: even if the Layer-5 scan is evaded, Layer 1 guarantees there
is no `os` table to call; the scan is belt-and-suspenders with good
ergonomics (offending token reported so the LLM can fix the script). This
whitelist + hard resource caps pattern is the canonical correct design for
sandboxing Lua 5.4.

---

## 4. Third-Party Library Usage

### 4.1 Current foundation (correct)

ESP-IDF 5.2 (NimBLE observer, FreeRTOS, `esp_pm`, USB-Serial/JTAG console,
VFS), LittleFS 1.14.8 via component registry, Lua 5.4.8 vendored (with a
staged registry alternative: `firmware/components/lua_alt/…disabled` and the
`LUA_SOURCE=vendored|registry` CMake switch), Unity for host tests.

### 4.2 Justified hand-rolling

AD-structure parser, JSON *encoder*, dedup table, CLI dispatcher, upload
bridge: small, fixed-schema, statically-buffered code where a library would
cost more (heap churn, API surface) than it saves. Notably, replacing the
JSON encoder with cJSON would be a regression — cJSON allocates per node in
a pipeline deliberately designed allocation-free. Hand-rolled JSON is only
dangerous for *parsing*, which the device never does (host→device traffic is
line commands, not JSON).

### 4.3 Where reuse would genuinely help (priority order)

1. **Replace the Lua pool allocator** — highest-value change in the repo.
   Options, ascending ambition:
   - ~30-line counting wrapper around `heap_caps_malloc/free` enforcing the
     byte limit; Lua's GC emergency collection handles reclamation.
   - Register the same 128 KB static array with ESP-IDF's heap via
     `heap_caps_add_region()` and allocate from it: ESP-IDF's battle-tested,
     coalescing allocator over reserved memory — determinism *and*
     correctness.
   - In-house fix: 32-bit offsets (or cap pool at 64 KB) + address-ordered
     free list with real coalescing (most work, least assurance).
2. **Enable the staged registry Lua port** (`UncleRus/esp-idf-lua`) to
   offload build maintenance; combine with the counting wrapper to keep the
   memory cap.
3. **Optional/taste:** `esp_console`/argtable3 for the CLI dispatcher (low
   urgency — the custom one is coupled to the JSON protocol and state
   machine by design); jsmn/cJSON from the registry *only if* device-side
   JSON parsing ever appears; run the same Unity tests on-target via
   ESP-IDF's managed Unity component to catch ESP-IDF-specific divergences
   the MinGW stubs can't.

### 4.4 The policy contradiction (see also §5.4)

`harness/00-global-context/project_overview.md` carries a "Library Reuse
Policy (MANDATORY)" declaring `proto_adv_parse.c` → NimBLE
`ble_hs_adv_parse_fields()`, `json_encoder.c` → cJSON, and the CLI →
`esp_console` as **"Redesign Required"**. The analysis behind it
(`docs/archive/design_patch.md`) was archived — the redesign was shelved —
but the mandate was never rescinded or annotated with the rejection
rationale. As shipped, the project **violates its own stated requirements
doc**. Either the policy should be updated ("evaluated and rejected:
zero-allocation pipeline requirement, fixed-schema output") or the
migrations done. Note `design_patch.md` correctly predicted the hand parser's
limits (UUID32/128 unsupported — still true in v1.0.0).

---

## 5. Feature-Requirement Evaluation (harness/ vs implementation)

### 5.1 Feature-by-feature verdict

| Feature | ID | Verdict | Notes |
|---------|----|---------|-------|
| USB CDC console | F0.1 | **Met + exceeded** | API exact match. CR/LF/CRLF all terminate (spec: `\n` only) — deliberate, documented in README. Overflow now drains the tail (H2) instead of spec's plain truncate; timeout discards partials (M1). Ctrl+C one-byte interrupt added. |
| LittleFS storage | F0.2 | **Met** | API exact match; 64 KB partition per `partitions.csv`; 8 KB script cap enforced. Hardware-verified via bridge suites. |
| AD parser | F1.1 | **Met** | 14 host tests ≈ spec's 14 TCs; pure-C constraint honored. Still UUID16-only (per spec — but see §5.4). |
| JSON encoder | F1.2 | **Met** | 19 host tests incl. escaping (H1). Output format matches spec exactly. |
| Filter engine | F1.3 | **Met** | 14 host tests; OR/AND combination, wildcards, case-insensitivity per spec. |
| NimBLE init | F2.1 | **Met w/ deviation** | Device name "BLE-Sniffer" set; observer-only sdkconfig (spec also listed `ROLE_BROADCASTER=y`) — leaner than spec, **deviation not documented in spec**. |
| BLE scan | F2.2 | **Met + exceeded** | Params validation ✓; dedup ✓ (128 entries vs spec's 64 — superset); queue reset on start/stop ✓. Overflow drops **newest + counts** vs spec's "drop oldest" — different choice, better observability, undocumented. |
| Scan pipeline | F2.3 | **Met + exceeded** | Core loop matches; Lua hooks implemented though spec said "future". `pipeline_set_filter_engine` renamed `pipeline_set_filter`. AC-8 queue flush satisfied via `ble_scan` start/stop (`ble_scan.c:209,252`), not `pipeline_stop`. Core-separation claim not enforced (§2.2 #3). |
| Lua port | F3.1 | **Met + exceeded w/ drift** | All 8 ACs pass (sandbox, `-612/-613/-614/-610`, deinit). Whitelist loading is *stronger* than spec's openlibs-then-remove. Drift: spec's `lua_engine_is_running` → impl `lua_engine_is_ready` + many additions; pool fixed 128 KB vs spec's 32/128 variants; timeout is a fixed instruction budget, not calibrated ms. |
| Script mgmt | F3.2 | **Met w/ drift** | begin/chunk/end ✓, 8 KB `-603` ✓, 5 s timeout `-605` ✓ (tick-domain), compile-before-save ✓, runtime hook errors fall through to default ✓. `on_adv` 7-arg signature now matches spec (M-S3-9). **`transform` drift: spec's 2nd param is a parsed table; impl passes the JSON string.** |
| CLI commands | F4.1 | **Met + exceeded** | All spec commands present; superset adds `LUA`, `POWER`, `SCRIPT STATUS`, `SCRIPT BEGIN/CHUNK` (F3.2 protocol). State machine `-911` ✓; Ctrl+C ✓; STATUS extended with `free_heap`, `lua_pool`. |
| LLM bridge | F4.2 | **Met w/ one deviation** | AC 1-3, 5-9 met (sandbox scan, `-803`, `-612` with compile message, empty-script reject). **AC-4 changed:** spec said a non-script command (e.g. `SCAN START`) aborts the upload; impl treats every line as script text except `SCRIPT END` (+ Ctrl+C always aborts). Reasonable, but a spec'd behavior was replaced. |
| Power mgmt | F4.3 | **Met via documented rescope** | Automatic tickless light sleep + `ESP_PM_NO_LIGHT_SLEEP` lock while scanning instead of manual sleep entry; USB suspend deferred to v2 (hardware exposes no suspend signal); `power_get_current_ma` returns calibrated estimates, not measurements. **Only spec with an "Implementation Notes (v1.0.0)" section documenting its deviations — the practice every other drifted spec should copy.** |

### 5.2 Test-tier reality vs specs

Every spec's "Test Files" table points at `tests/harness/test_*_on_target.c`
— **that directory does not exist**. The on-target tier was replaced by:
`tests/host/` (76 Unity cases — exactly the 76/76 claimed) plus root-level
Python HIL suites (102 checks) and `soak_test.py`. Coverage outcome is
arguably better than the spec'd plan, but all 13 specs' test-file references
are stale.

### 5.3 Where requirements genuinely diverge

Unmet as-specified, all conscious decisions:

1. **F4.3 USB suspend** ACs 4-6 — deferred to v2, documented in the spec.
2. **F4.2 AC-4** abort semantics — replaced by SCRIPT END / Ctrl+C model.
3. **Library Reuse Policy (MANDATORY)** in `project_overview.md` — not
   followed; the backing analysis sits in `docs/archive/` while the mandate
   text remains active and unannotated.

### 5.4 Process findings

- **Spec maintenance is one-sided.** F4.3 got an implementation-notes
  update; F2.1 (broadcaster role), F2.2 (drop-newest, 128-entry dedup),
  F2.3 (renamed API, no core pinning), F3.1 (API drift), F3.2 (`transform`
  signature) did not — their deviations live only in code comments and
  status docs. Anyone testing against the specs today would report false
  failures (e.g. `pipeline_set_filter_engine`, `lua_engine_is_running`,
  `transform(addr, table)`).
- **The policy/spec/implementation triangle is inconsistent** (§4.4): the
  overview mandates built-ins, the specs describe custom modules, the
  implementation ships custom modules. One authoritative statement is
  needed.
- **Positive:** acceptance criteria were written testably (codes, states,
  exact JSON), which is why compliance was checkable at all; the
  bug-ID-citation habit in code (`B-S3-1`, `M-S3-9`, `H2`, `H4`…) makes the
  traceability unusually good.

---

## 6. Recommendations (ordered)

1. **Fix or replace the Lua pool allocator** (`lua_port.c`): 32-bit offsets
   or pool ≤ 64 KB, plus coalescing — or adopt §4.3 option 1 and delete the
   custom pool. This is the only outright correctness risk found.
2. **Decouple filter-engine locking** from the Lua mutex
   (`scan_pipeline.c:68`): give the filter engine its own mutex; cache the
   `has_func` checks per script-run instead of twice per advertisement.
3. **Reconcile the docs:** rescind/annotate the MANDATORY reuse policy with
   the rejection rationale; back-port implementation-notes sections (à la
   F4.3) into F2.1/F2.2/F2.3/F3.1/F3.2; fix or delete the `tests/harness/`
   references.
4. **Decide and document the bridge abort semantics** (F4.2 AC-4) as they
   actually ship.
5. Optionally: enable the staged registry Lua port; consider on-target
   Unity runs; pin the NimBLE host task or correct the spec's core-separation
   claim.
