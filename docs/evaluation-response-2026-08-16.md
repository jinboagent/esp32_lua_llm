# Response to Project Evaluation (2026-08-15)

**Date:** 2026-08-16
**Responding to:** `docs/evaluation-report-2026-08-15.md`
**Method:** every claim re-verified against source before agreeing; agreed
code/doc items fixed in the same round; disagreements argued below.

## Agreed and acted upon

| # | Finding | Action taken 2026-08-16 |
|---|---------|------------------------|
| 1 | **Pool allocator unsound** (uint16 offsets vs 128 KB; no coalescing; "sorted" comment false) | Extracted to `firmware/components/lua/lua_pool.c` + `interfaces/lua_pool_if.h`: uint32 offsets/sizes, address-ordered free list with real on-free coalescing, top blocks returned to the bump pointer, single ledger. 10 new host tests incl. a >64 KB offset regression the old allocator would fail (free-list offset-0 sentinel collision also fixed). |
| 2 | **Lock coupling** (Lua mutex around C filter; `has_func` twice per adv) | Filter engine now owns its own mutex (`filter_lock/unlock`, no-op under `HOST_BUILD`); pipeline and CLI use it instead of `lua_engine_lock`. Hook presence cached at SCRIPT RUN/STOP (`script_has_on_adv/transform`) — zero Lua lock round-trips per advertisement. |
| 3 | **Core-separation claim not enforced** (F2.3) | Spec annotated (Implementation Notes): NimBLE host task is not pinned; priorities decouple the work. Chose annotation over pinning — pinning would fight `nimble_port_freertos_init`'s internal task creation for a v1 with no measured contention. |
| 4 | **Textual sandbox scan false positives** | Accepted and documented (F3.1/F4.2 notes): fail-closed by design; Layer-1 whitelist is the security boundary, the scan is ergonomics (reports the offending token). |
| 5 | **Spec drift** (F2.1 broadcaster off, F2.2 drop-newest + 128 dedup, F2.3 renames, F3.1 API drift, F3.2 `transform(addr, json_string)`) | Implementation-Notes sections back-ported into each spec, copying the F4.3 practice the eval praised. |
| 6 | **Stale `tests/harness/` references** | All spec Test-File rows rewritten to the real tiers (`tests/host` Unity + root Python HIL suites). |
| 7 | **Policy contradiction** (`project_overview.md` MANDATORY reuse list) | Policy annotated with the rejection rationale and a pointer to this doc. |
| 8 | **v2 headroom / main-loop polling / scratch logs** | Accepted as observations; no v1 action (logs are gitignored; polling impact measured small). |

## Disagreements (with arguments)

1. **F4.2 AC-4 "impl treats every line as script text except SCRIPT END"** —
   factually wrong. `cli_commands.c` (`cli_process_command`, upload-mode
   branch): any *recognized* CLI command aborts the upload (`bridge_abort`)
   and then dispatches normally — exactly the spec'd AC-4; only
   *unrecognized* lines are script text, and Ctrl+C always aborts. The
   eval appears to have read the branch comment's first line only.

2. **"ESP-IDF 5.2"** (§4.1) — we build against **v5.1**
   (`C:/Espressif/frameworks/esp-idf-v5.1`, README, sdkconfig). Erratum.

3. **Two ledgers "can drift on edge paths"** (§2.2 #1) — theoretical only:
   Lua only ever frees pointers the allocator returned, and both update
   sites used identical charge math. Moot now anyway: the extraction
   deleted the mirrored ledger; the pool is the single source of truth.

4. **Lock-coupling severity** (§2.2 #2) — direction agreed (and fixed), but
   the "serializes the pipeline" framing overstates it: an uncontended
   FreeRTOS mutex take is sub-microsecond; the only real serialization is
   a script near its 10 ms budget, which is the intended CPU confinement,
   not a locking defect.

5. **Switch to the registry Lua port now** (§4.3 rec 2) — declined for v1:
   vendored sources give reproducible builds and the registry port is
   already staged behind `LUA_SOURCE=vendored|registry`. Switching now adds
   risk without benefit; revisit at v2 when touching the build anyway.

6. **`esp_console` for the CLI** (§4.3 rec 3) — declined, rationale
   documented: the operator is a machine (host LLM tooling); the protocol
   needs prompt-free single-line JSON, state guards, zero allocation, and a
   multi-line upload mode — everything esp_console doesn't provide, while
   its gifts (history, tab completion, help text) are stream pollution.
   The original plan's esp_console mandate was evaluated and rejected; the
   policy annotation now records this.

7. **Repo rename** (§2.2 #6) — won't-fix: `esp32_lua_llm` names the product
   loop (LLM on the host generates the Lua); the README states explicitly
   that no LLM/HTTP/TLS lives in the firmware. Renaming costs CI, paths,
   and bookmarks for a cosmetic gain.

8. **cJSON for the encoder** — the eval itself concludes replacement would
   be a regression (per-node heap allocation in an allocation-free
   pipeline); recorded as agreement, cited in the policy annotation.

## Verification

- Host: **86/86** (76 prior + 10 pool tests; `-Wall -Wextra -Werror`)
- Firmware: build + flash + `test_bridge_hw.py` / `test_power_hw.py` /
  `test_ble_lua_hw.py` regression run as part of this round (results in
  `status/LATEST.md`)
- Optional follow-up: re-run `soak_test.py` (2 h) to re-bless the new
  allocator under the fragmenting workload
