# H6.1 Branch Audit — `Lua_tool_extension_dev` vs `master`

> **Author:** zcode · 2026-09-07 · Pre-merge bug audit of the 8 H6.1 commits
> (`2b21853..9d93fc9`) sitting on `Lua_tool_extension_dev` ahead of master
> (`d0b73a3`, merge-base). Method: two independent line-by-line review passes
> (firmware diff, host diff) cross-checked against full current sources and
> callers, plus the four worst findings re-verified by hand in this session.
> Automated suites all green (below) — every finding here is a live-code
> defect the suites do not currently catch.

## Verification state (all green — context for the findings)

| Check | Branch (`9d93fc9`) | Master (`d0b73a3`) |
|---|---|---|
| Unity host tests | **147/147** (11 suites, 0 failures) | **126/126** (9 suites, 0 failures) |
| `test_assistant.py` | **144/144** | 49/49 |
| `test_run_case.py` | 35/35 | 35/35 |
| ESP-IDF firmware build | clean, exit 0, **no warnings** | not rebuilt (unchanged by branch) |

Master itself is healthy: no new bugs found on master beyond the pre-existing
items P1–P4 below (all in files the branch touches).

## Findings index

| ID | Sev | Area | One-liner |
|----|-----|------|-----------|
| B1 | HIGH | fw | `PACK END` compile error emitted as invalid JSON (unescaped) |
| B2 | HIGH | host | Pack validation fail-open when manifest assembler fails |
| B3 | MED | fw | `.autorun` markers eat the 8-dirent cap → packs silently missing |
| B4 | MED | host | Native tool cap checked per message, not per call; no round cap |
| B5 | MED | host | `--mutating-gate` detection bypassed by aliasing |
| B6 | MED | host | Deploy path: no per-line 255-byte check; device silently drops lines |
| B7 | MED | fw | Ctrl+C does not abort PACK / LUA-chunk upload sessions |
| B8 | MED | host | `c["id"]` KeyError on providers omitting tool-call ids → session abort |
| B9 | MED | host | `/tools load @name` runs pack on-device before collision check |
| B10–B19 | LOW | both | See "Low severity" section |
| P1–P4 | pre-existing | fw | On master before the branch; in files the branch touches |

---

## HIGH

### B1. `PACK END` emits invalid JSON when the pack fails to compile — fw [hand-verified]

`firmware/components/cli/cli_commands.c:813-817`, fed by
`firmware/components/pack_store/pack_store.c:124-128`:

```c
char cerr[96];
if (lua_engine_compile_check((const char *)s_buf, cerr, sizeof(cerr)) != 0) {
    s_set_err(err, err_len, cerr);          /* raw Lua error text */
    return -612;
}
```
```c
CLI_EMIT(response, response_len,
    "{\"status\":\"error\",\"cmd\":\"pack_end\","
    "\"code\":%d,\"msg\":\"%s\"}", ret, err);   /* err NOT escaped */
```

`luaL_loadstring` errors look like `[string "pack"]:1: unexpected symbol ...`
— they contain double quotes. `err` goes into `"msg"` raw, so a pack with any
syntax error produces a response line that is not strict JSON. This is the
branch's primary self-correction loop (LLM-generated pack → compile error →
host shows it to the LLM), and it violates the repo's tested contract ("every
CLI response strict-JSON validated", `tests/host/test_cli_responses.c`).

Same bug class as eval-2026-08-11 H1 (fixed then via `json_escape_str` on the
SCRIPT paths) — reintroduced on the new path. Every comparable emit escapes:
`h_lua` EXEC/END (`cli_commands.c:472-475`, `:526-529`), `PACK RUN`'s result
(`cli_commands.c:827-828`), the F4.2 bridge (`lua_llm_bridge.c:253-259`).
Missed by tests because the host stub returns a quote-free error string
(`tests/host/test_stubs.c:199-204`).

Trigger: `PACK BEGIN t` → `x =` → `PACK END`.
Fix: `json_escape_str(err)` before emit, like `bridge_upload_finish`.

### B2. Host-side pack validation is fail-open when the manifest can't be assembled — host [hand-verified]

`host_app/assistant.py:1748-1761` (`do_tools_load`):

```python
mtext, asm_err = assemble_manifest_from_source(src)
if asm_err is None:
    m, verr = validate_manifest(mtext)
    ...collision checks...
# falls through to confirm + send with NO checks when asm_err is set
```

`assemble_manifest_from_source` (`assistant.py:462-491`) only recognizes
manifests built from `VAR = [[...]]` / `VAR = VAR .. [[...]]` long-bracket
literals with a one-line `function manifest() return VAR`. Any other legal
shape — quoted strings, `string.format`, `table.concat`, multi-line `[[` —
returns an error, and then the **entire validation/collision block is
skipped**: the pack is confirmed and sent with zero host-side checks.

The comment at `assistant.py:1745-1747` states the spec (AC#2: collisions
rejected "before any device line is sent"); the code contradicts it on this
path. Verified: a quoted-string pack passes `lua_exec_lines` but the
assembler returns `no 'M' string literal found` → fail-open path taken.
Consequence: a colliding pack's lines EXEC into the live Lua state,
overwriting the earlier pack's same-named functions (no reset, no rollback);
`tools.add` only rejects afterwards — device already polluted.

This is a *natural LLM output style*, not an edge case. Spec:
`harness/01-features/stage6-agent/feature_tool_registry.md` AC#2.
Fix: assembler failure must reject (or fall back to fetch-validate-refuse),
never silently skip.

---

## MEDIUM

### B3. Autorun marker files consume the readdir cap — packs silently vanish — fw [hand-verified]

`firmware/components/pack_store/pack_store.c:192-194` (`pack_store_list`) and
`:268-270` (`pack_store_boot_autorun`):

```c
storage_dirent_t ents[PACK_MAX_FILES];      /* 8 dirents, not 8 packs */
int ret = storage_list_dir(PACKS_DIR, ents, PACK_MAX_FILES, &count);
```

Each autorun pack occupies **two** dirents (`<name>.lua` + `<name>.autorun`);
`storage_list_dir` (`littlefs_storage.c:197`) stops at 8 dirents total. With
5+ packs (several autorun), entries past the first 8 are never seen:
`PACK LIST` omits packs with no error, and `pack_store_boot_autorun` silently
never runs the cut-off packs — defeating the feature's "tools alive before
any host connects" promise. `interfaces/pack_if.h` documents `PACK_MAX_FILES`
as a *pack* count.

Trigger: upload 6 packs, set autorun on ≥3 → reboot → only packs whose
entries land in the first 8 direns run.
Fix: list with `2*PACK_MAX_FILES` entries (both call sites), or filter
markers with continued reads.

### B4. Native tool cap checked per message, not per call; no round cap — host [hand-verified]

`host_app/assistant.py:1244-1258` gates once *before* the per-call loop at
`:1262-1305`; `executed += 1` at `:1294` with no re-check inside the loop.
One assistant message with N parallel `tool_calls` executes all N regardless
of `TOOL_EXEC_CAP`. Simulated: 5 calls in one message with cap 3 → 5
executions. Additionally the `while True` in `ask_llm_native` has **no round
counter** — a model that keeps emitting tool_calls after the cap loops
forever (each iteration = one paid API call, context growing unbounded). The
prompt-emulated path has an explicit hard stop (`assistant.py:1395-1414`).
OpenAI-style providers routinely batch parallel calls, so this is the
expected production shape.
Fix: check the cap inside `for c in calls`, and add a round cap.

### B5. `--mutating-gate` static detection bypassed by aliasing — host

`host_app/assistant.py:1649-1659` (`mutating_names_in`) only matches
`(?<!\w)name\s*\(`. Verified undetected: `local f=bench_reset return f({})`,
`return _G["bench_reset"]({})`, `return ("bench_".."reset")({})`. With the
gate on, an injected/creative program calls `pin_write`/`cfg_set` with no
confirmation. Spec d10 says the gate confirms programs calling
manifest-declared mutating tools. Note `tests/host/test_assistant.py:1703-1710`
asserts the alias blind spot as *correct* behavior (see test findings).

### B6. Deploy path: no per-line length check; >255-byte lines silently dropped — host

`host_app/assistant.py:1455-1468` (`deploy_lua` → `upload_script`,
`assistant.py:845-883`). The system prompt (`assistant.py:139-141`) tells the
LLM only "under 8192 bytes" — nothing about per-line limits. Firmware
`usb_console_read_line` (`usb_cdc_console.c:133-163`) discards an overlong
line and returns `-504`, which `main.c` reports as **plain text**
(`Read error: -504`), not JSON — the upload loop's error matcher
(`assistant.py:872`) never fires. Result: script stored without the line; if
the dropped line was self-contained (e.g. `threshold = -70`) the deployed
filter **silently differs from the code shown at the `deploy? [y/N]` prompt**.
Compounding: `lua_exec_lines` measures `len(line)` in **characters**
(`assistant.py:395`) against a 255-**byte** device budget — CJK/° chars pass
host checks then get dropped.
Fix: 255-byte per-line check on the deploy path too + tell the LLM the limit.

### B7. Ctrl+C does not abort the new PACK / LUA-chunk upload sessions — fw

`firmware/components/cli/cli_commands.c:1133-1168` (`h_interrupt`) aborts
bridge upload, script, conn, scan — but never calls `pack_store_abort()` or
`s_lua_chunk_abort()`. The comment (`:1130-1132`) and main.c's banner
("Ctrl+C: stop scan/script/upload/connection immediately") are now false for
the M2/M3 upload modes. After Ctrl+C mid-`PACK BEGIN`, non-command lines keep
appending to the aborted-in-spirit pack (and `LUA BEGIN` accumulates and
executes at the next `LUA END`). Recovery still possible via any recognized
command → fail-open behaviorally, not memory-unsafe.

### B8. `c["id"]` KeyError on providers that omit tool-call ids — host [hand-verified]

`host_app/assistant.py:1254, 1271, 1278, 1291, 1304` index `c["id"]` directly
(the function name is `.get()`-guarded, the id is not). OpenAI-compatible
endpoints that return tool_calls without `id` (several self-hosted/proxy
servers) raise KeyError; `handle_line` catches only `RuntimeError`
(`assistant.py:1908-1911`) → whole session aborts. needs-verification: which
endpoints omit ids, but the code's own defensive style shows ids were
expected optional elsewhere.

### B9. `/tools load @name` clobbers earlier packs on-device before collision check — host

`host_app/assistant.py:1823-1836` (`do_tools_run_device`): `PACK RUN`
executes the stored pack into the live Lua state **first**; manifest fetch /
validation / collision check happen only afterwards in `tools.add`. Firmware
`pack_store_run` (`pack_store.c:152-186`) is a plain `lua_engine_exec` —
same-named globals overwritten, no rollback. On collision the host registers
nothing, but the device now runs the new pack's code under the old registry
entries; host cache and device silently diverge (exactly what decision 8's
collision rejection exists to prevent). No pre-check possible on this path;
needs a device-side reset/re-register strategy or a documented restriction.

---

## Low severity

- **B10** trailing-space variants (`"PACK END "`, `"LUA END\t"`) abort the
  upload instead of finishing it — dispatch matches exactly
  (`cli_commands.c:1197-1214`, `h_lua` `:509`); the session aborts via the
  recognized-command path and data is silently discarded with a misleading
  syntax error.
- **B11** `PACK RUN` discards the real Lua error text (`result` holds it) and
  masks -614 instruction-budget timeouts as -613: `pack_store.c:180-184`
  (`"exec failed (%d)"`). Hurts the LLM repair loop.
- **B12** `hw.*` pin whitelist (`lua_hw.c:43-47`) exposes UART0 (43/44) and
  strapping pins (3, 45, 46) to *persistent autorun* packs; USB CDC keeps
  working so it stays recoverable, but it is a standing hardware-policy
  hazard. needs-verification against the actual dongle board mapping.
- **B13** `pack_store_list` underflow landmine: `json_len - off` wraps if
  called with `json_len < 43` (only `== 0` guarded,
  `pack_store.c:190-204`, `:219-223`) — unreachable today (512/1024 callers)
  but exported via `interfaces/pack_if.h`.
- **B14** `PACK LIST` silently truncates when the 512-byte response budget
  runs out — no `truncated` marker (`pack_store.c:219-229`); worst case 8
  packs × 24-char names + autorun flags. Host tests use a 1024 buffer, so
  the firmware's 512 class is untested.
- **B15** `lua_args_literal` (`assistant.py:1001-1017`): Python
  `json.loads` accepts `Infinity`/`NaN` → `inf`/`nan` literals are nil in
  Lua; `json.dumps` control-char escapes (`\u0001`) are invalid Lua 5.4.
- **B16** native-mode prompt self-contradiction: `self.tools.prompt()`
  (envelope instructions, `assistant.py:577-591`) concatenated with "reply
  with plain text" (`assistant.py:1216-1220`).
- **B17** misc host gaps: duplicate arg names within a tool not rejected
  (`assistant.py:407-416`); `fetch_manifest` byte/char mismatch at chunk
  boundaries → U+FFFD mojibake for non-ASCII manifests
  (`assistant.py:1594-1616`); partial pack registration leaves lines 1..i-1
  as live globals with no unregister (`assistant.py:1773-1783`); `DEPLOY_RE`
  misses indirect hook assignment (`manifest = mf` classified as a tool
  program, `assistant.py:116-118`); `cmd_json`'s `reset_input_buffer()`
  (`assistant.py:802`) drops buffered adv lines before every command —
  pre-existing pattern, amplified by the new per-line EXEC loops.
- **B18 (design-level)** prompt-injection chain: attacker-controlled BLE
  adv name/manufacturer text flows verbatim into prompts
  (`snapshot` `assistant.py:642-665`, `build_messages` `:1351-1361`); with
  hwio registered and `--mutating-gate` off (default), a persuaded LLM's
  tool program drives GPIO / rewrites NVS config autonomously. The gate
  exists but is opt-in; no banner warning for hwio + default settings.
- **B19** pack/chunk upload lines beginning `PACK `/`LUA `/`SCRIPT ` are
  intercepted as commands mid-upload (fail-closed abort). Pre-existing
  shared-namespace design newly extended to pack bodies; note-only.

## Tests asserting wrong / missing behavior (`tests/host/test_assistant.py`)

1. `:1703-1710` `test_names_detected_from_manifest` — asserts the B5 alias
   blind spot is correct, locking in the bypass.
2. `:1273-1282` `test_multiline_string_falls_back` — comment says caller
   "falls back"; the caller actually falls back to *no check at all* (B2).
   Enshrines the fail-open misunderstanding.
3. `:1898-1922` `test_native_cap_skips_and_forces_final` — only single-call
   messages; B4's parallel-call overshoot untested.
4. Coverage gaps: native-mode mutating-gate branch (`assistant.py:1282-1293`)
   has zero tests; no >255-byte-line test anywhere (B6); no test for a pack
   whose manifest assembler fails (B2's fail-open path); no quote-bearing
   compile-error stub (B1).

## Pre-existing on master (in files the branch touches — NOT introduced here)

- **P1** `LUA EXEC` performs no forbidden-token scan at all
  (`cli_commands.c:459-487`, identical on master — only the library
  whitelist gates one-line execs; the branch now also exposes `hw.*` through
  it, e.g. `LUA EXEC return hw.gpio_write(43,1)`).
- **P2** `usb_console_read_line` silently drops >255-byte lines
  (`usb_cdc_console.c:133-164`) — interacts with B6.
- **P3** `s_check_path` validates length only, no canonicalization
  (`littlefs_storage.c:41-46`); currently safe because callers validate name
  charset (incl. the new pack/kv code).
- **P4** bare `LUA` help omits `BEGIN|END` (`cli_commands.c:1235`) while
  `h_lua`'s own error and the main.c banner were updated.

## Recommended fix order (pre-merge)

1. **B1** — one-line `json_escape_str` fix; add a quote-bearing compile-error
   stub to the response-contract corpus.
2. **B2** — assembler failure must *reject* the pack (spec AC#2 is explicit).
3. **B3** — `2*PACK_MAX_FILES` dirents at both call sites + a hw regression
   (6 packs, 3 autorun, reboot, all 3 run).
4. **B4** — cap check inside the per-call loop + round cap.
5. B5/B6/B7 next; B8–B19 can ride a follow-up commit if needed.

## Session evidence

- `git merge-base master HEAD` → `d0b73a3`; branch = 8 commits, +6008/−33.
- Unity (branch): fresh `/tmp/hostbuild`, 11 suites — 147 tests, 0 failures.
- Unity (master, worktree `d0b73a3`): 9 suites — 126 tests, 0 failures,
  zero compiler warnings.
- Python: branch 144 + 35 OK; master 49 + 35 OK.
- Firmware: full `idf.py build` (ESP-IDF v5.1, cmd //c recipe) — exit 0,
  no warnings.
- B1/B2/B3/B4/B8 re-verified by direct source reads this session (file:line
  above); remaining findings agent-verified against firmware + host sources
  with runnable Python checks where noted.
