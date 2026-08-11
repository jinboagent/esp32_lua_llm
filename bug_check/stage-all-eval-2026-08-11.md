# v1.0.0 All-Stage Re-Evaluation + BLE/Lua Hardware Push — 2026-08-11

> ✅ **RESOLVED 2026-08-11** — H1/H2/H3 fixed and regression-verified the
> same day ([`fix-report-2026-08-11-eval-h1-h3.md`](bug_fix_report/fix-report-2026-08-11-eval-h1-h3.md)).
> H4 remains a soak-test watch item. The "OPEN" banner below is the
> historical evaluation state, kept for traceability.
>
> ⚠️ **OPEN (at evaluation time)** — 3 new firmware findings (H1 medium, H2 low, H3 medium),
> 1 watch item (H4), and 4 broken/stale test scripts (T1–T4).
> Two new hardware suites landed today: `test_ble_lua_hw.py` (45 checks)
> and `test_ble_peer_hw.py` (11 checks, PC acting as controlled BLE peer).

Scope: full code audit of the current tree (v1.0.0 + N1/N2 fixes), test
sufficiency assessment, and live hardware verification on COM12 with new
focus on the **BLE data plane** and **Lua on target** — the two areas the
previous hardware suites never truly exercised.

---

## Live verification snapshot (COM12, 2026-08-11, 20:10–20:40)

| Suite | Result | Notes |
|-------|--------|-------|
| `test_ble_lua_hw.py` (NEW) | **43/45** | 2 failures are H1/H2 tracked below |
| `test_ble_peer_hw.py` (NEW) | **11/11** | PC advertises via WinRT; dongle captures exact fields |
| `test_bridge_hw.py` | 32/32 (2026-08-10) | not re-run today |
| `test_power_hw.py` | 14/14 (2026-08-10) | not re-run today |
| host suite | 67/67 (2026-08-10) | not re-run today |

New positive evidence gathered today:

- 7-arg `on_adv` ABI proven **on device** via an introspection script
  (`select('#', ...)` → 7; types start `string,number,number`).
- `on_adv` suppression-all → exactly 0 adv lines for 6 s (deterministic).
- `transform` rewrite → 100 % of lines carried the injected marker; marker
  gone after `SCRIPT STOP`.
- Dedup window invariant held on ambient RF (no addr twice within ~1 s) and
  dedup cadence on a continuous controlled peer ≈ 1 line/s.
- Sandbox codes verified live: `-612` compile, `-614` timeout, blocked libs nil.
- Ctrl+C (single 0x03) stops the stream; state returns to idle.
- **Connection boundary (v1)**: while the dongle scans, the PC (bleak
  central) scanned back and could **not** discover it — the dongle does not
  advertise and exposes no GATT server, so PC↔dongle connections are
  impossible in v1 by design (see v2 plan below).
- Radio contention: dongle kept streaming while the PC adapter ran its own
  active BLE scan.

---

## Firmware findings

### 🟠 H1 (medium). Lua error/result text is interpolated unescaped into JSON responses → invalid JSON

**Files**: `cli_commands.c` `h_lua()` (`"result\":\"%s\""` / `"msg\":\"%s\""`
with `lua_result`), `lua_llm_bridge.c` `bridge_upload_finish()`
(`"msg\":\"%s\""` with the Lua compile error).

Lua error messages embed double quotes (`[string "..."]`), and script
return values can contain quotes/newlines. Both are spliced into JSON with
plain `%s`, producing lines no JSON parser accepts.

**Live evidence** (LUA EXEC, 2026-08-11):

```text
{"status":"error","cmd":"lua_exec","code":-612,"msg":"[string "return +++"]:1: unexpected symbol near '+'"}
```

**Live evidence** (SCRIPT END compile-error — the LLM bridge's key error path):

```text
{"status":"error","cmd":"script_end","code":-612,"msg":"[string "return +++..."]:1: unexpected symbol near '+'"}
```

**Impact**: host tooling (and the LLM loop itself) fails to parse the very
responses that matter most — syntax errors are frequent when an LLM
generates scripts. Also affects `LUA EXEC` success results containing
quotes/newlines.

**Why tests missed it**: host tests match response substrings
(`strstr`), never strict-`json.loads` the line.

**Fix direction**: one small JSON string escaper used for every dynamic
text field (`msg`, `result`, filter `pattern` echo). Add strict JSON-parse
assertions to host CLI/bridge tests.

---

### 🟡 H2 (low). Overlong line (-504) leaves its tail in the RX stream, re-parsed as a command

**File**: `usb_cdc_console.c` `usb_console_read_line()` overflow branch.

At `pos == buf_len-1` (USB_RX_BUFFER_SIZE = 256) the function returns -504
without consuming the rest of the line. The remaining bytes become the next
"command".

**Live evidence** (300 × 'X' sent):

```text
Read error: -504
> XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX     <- tail re-read as command
{"status":"error","msg":"unknown command"}
```

**Impact**: benign with well-behaved hosts; garbage commands (and echo
noise) with malformed input. Robustness issue, not a safety issue.

**Fix direction**: on overflow, keep reading (discarding) until CR/LF,
then return -504.

---

### 🟠 H3 (medium). Documented 512-byte SCRIPT CHUNK is impossible over the 255-char USB command line

**Files**: `script_if.h` ("len  ... max 512 recommended") vs
`usb_if.h` `USB_RX_BUFFER_SIZE 256`.

`SCRIPT CHUNK <hex>` rides one command line: 13 char prefix + 2×payload
must fit in 255 chars ⇒ **max real chunk = 121 bytes**. Any host following
`script_if.h` guidance (e.g. `test_hooks.py` with CHUNK_SIZE=200 ⇒ 413-char
lines) silently overflows (H2 path) and corrupts the upload without a
single error response referencing the chunk.

**Evidence**: `test_bridge_hw.py` hex-path regression uses an 8-byte chunk
("return 1"), so the limit was never exercised; the stale hook scripts
(T1/T2) fell into this trap.

**Fix direction** (pick one):
1. Document the 121-byte transport limit in `script_if.h` + README, and/or
2. raise `USB_RX_BUFFER_SIZE` to ≥ 1100 (one 512-byte chunk + prefix),
   re-validate stack/heap impact.

---

### ⚪ H4 (watch item, unverified). Lua pool allocator: no coalescing/splitting → long-run fragmentation risk

**File**: `lua_port.c` (`s_pool_alloc` first-fit without splitting,
`s_pool_free` LIFO push without coalescing).

A script that runs for hours and allocates strings in `transform` on every
advertisement could fragment the 128 KB pool until allocations fail even
with enough total free bytes. There is **no runtime observability**: STATUS
carries no Lua pool used/peak figure (peak is only printed at `LUA DEINIT`).

**Action**: not a confirmed bug — verify with the soak test in the hardware
plan below before changing anything. Adding `lua_used`/`lua_peak` to STATUS
would make the soak measurable.

---

## Broken / stale test scripts (T1–T4)

These ad-hoc scripts live at the repo root and their past verdicts are
**not trustworthy**:

| ID | Script | Problem |
|----|--------|---------|
| T1 | `test_hooks.py`, `test_hooks2.py` | Use the **pre-M-S3-9 3-arg signature** `on_adv(addr, rssi, name)` while firmware passes 7 args. Positional shift makes `name` ← rssi (always truthy) ⇒ the "only named devices" filter passes **everything**. Proven live today: with the legacy script running, unnamed devices leaked through (suite check L6). |
| T2 | `test_hooks.py`, `test_hooks2.py`, `test_script.py` | 200–256-byte `SCRIPT CHUNK` payloads ⇒ > 255-char lines ⇒ H2/H3 overflow ⇒ uploads were silently corrupted. The scripts never validated upload success. |
| T3 | `test_script.py` | Test 3 explicitly skips the upload ("testing CLI commands" only). |
| T4 | `test_bugfixes.py` | Test 4 expects `SCRIPT RUN` to work from IDLE; the state machine rejects that (`-911`) since F4.1. |

**Recommendation**: superseded by `test_ble_lua_hw.py`; either delete the
stale scripts or mark them `# SUPERSEDED 2026-08-11 — see test_ble_lua_hw.py`.

---

## Test sufficiency verdict

| Layer | Before today | Verdict |
|-------|--------------|---------|
| Host (67 Unity tests) | proto/json/filter/cli/bridge logic | Good, **but** string-matched JSON (H1 slipped through) and no coverage of the Lua VM layer or dedup logic (FreeRTOS-bound; stubbable like the other modules) |
| HW command plane | bridge 32 + power 14 | Sufficient |
| HW BLE data plane | "some adv lines appeared" only | **Insufficient** → now covered by `test_ble_lua_hw.py` L2 + `test_ble_peer_hw.py` P1/P2 |
| HW Lua data plane | broken scripts only (T1/T2) | **Insufficient** → now covered by L3–L8 |
| HW long-run / soak | one 25 s capture | **Insufficient** → plan below |
| HW connection / GATT | none (v1 scan-only) | By design absent → v2 plan below |

Bottom line: the release's *command plane* was well tested; the *BLE and
Lua data planes* were effectively untested on hardware. Today's two new
suites (56 checks) close most of that gap; soak + connection testing remain.

---

## Hardware test plan (focus: BLE + Lua + connections)

### Already landed today

- **`test_ble_lua_hw.py`** — 45 checks: JSON schema/ts-monotonicity/dedup
  invariants on ambient RF; sandbox codes on target; 7-arg ABI proof;
  suppress-all on the stream; legacy-signature leak demo; transform
  rewrite; overlong line; Ctrl+C.
- **`test_ble_peer_hw.py`** — 11 checks: PC as controlled BLE peer via
  WinRT advertiser (mfg id FFFF, payload `SNF AABBCC`); exact field
  verification; dedup cadence; v1 non-discoverability; radio contention.

### Next session (short term)

1. **Fix H1/H2/H3**, re-run both new suites (target 45/45 + 11/11) and the
   existing bridge/power suites.
2. **Soak (2–4 h)**: continuous SCAN + a transform script active; sample
   STATUS every 5 min; pass = no reset_reason change, no queue_drops
   growth, no output stall, stable pipeline counters. Needs H4
   observability (`free_heap` + `lua_used/peak` in STATUS) first.
3. **Stress**: SCAN INTERVAL 10 in dense RF (watch queue_drops), 100×
   SCAN START/STOP cycles, FILTER ADD to max rules, 8 KB-1 script via
   bridge, SCRIPT BEGIN→30 s silence→`-605` timeout path, Ctrl+C
   mid-upload/mid-scan matrix.
4. **Regression pin**: add the new suites to the README "Hardware suites"
   list once H1/H2 are fixed.

### v2 connection tests (roadmap — "BLE connection with the PC")

v1 baseline pinned today (P3): dongle is not discoverable/connectable.

- **Central role (dongle connects out)**: controlled peer GATT server on
  the PC (bleak) or a second ESP32; tests: connect time, scan+connect
  concurrency, GATT read/notify → JSON output format, disconnect/reconnect
  policy, addr whitelist, scan-throughput impact of connection events.
- **Peripheral role (PC connects to dongle)**: firmware must first grow an
  advertiser + GATT control service (start/stop scan over a characteristic);
  tests: bleak connect from PC, control parity with USB commands, USB+BLE
  command arbitration, security mode (pairing/bonding policy TBD).
- **Environment note (this machine)**: Windows 10 build 19045, MediaTek
  combo adapter — LE + central + peripheral roles supported; WinRT
  `local_name` advertising fails with E_INVALIDARG, manufacturer-data-only
  payloads work (quirk documented in `test_ble_peer_hw.py`). bleak 3.0.2 +
  winrt 3.2.1 installed via pip.

### Product-loop field test

With the data plane now verified, run the full loop: capture ambient JSON →
host LLM generates a Lua filter → `SCRIPT LOAD` → verify suppression with
`test_ble_lua_hw.py`-style assertions.

---

## Environment / tool deltas this session

- Installed (pip): `bleak 3.0.2`, `winrt-*` 3.2.1 family.
- New files: `test_ble_lua_hw.py`, `test_ble_peer_hw.py`.
- No firmware sources were modified in this evaluation.
