# Fix Report — F2.4 GATT Data Path (First Working Run)

| Field   | Value |
|---------|-------|
| Date    | 2026-08-28 |
| Scope   | `firmware/components/ble/ble_conn.c`, `firmware/components/json_enc/json_encoder.c`, `tests/hw/test_ble_conn_hw.py`, `tests/host/test_ble_conn.c` |
| Result  | **C1–C6 GATT data path works for the first time** — hw suite 43 passed / 0 failed / 2 informed SKIPs; host suite 22/22 |
| Branch  | `fix/f24-gatt-data-path` |

---

## 1. Background

The F2.4 GATT data path (hw tests C1–C6) had **never executed** against real
hardware: every run since 2026-08-22 skipped at peer startup because the
test-side `GattPeer` drove WinRT async APIs synchronously
(`E_ILLEGAL_METHOD_CALL`). That harness bug was fixed earlier on 2026-08-28
(asyncio/await port per `vendor_reference/ble_test/`), which unblocked C1–C6
for the first time — and exposed two firmware defects that had been hiding
behind the skip ever since F2.4 landed.

## 2. Defect 1 — discovery callbacks treated arriving records as errors

**Symptom.** The dongle connected, the MTU exchange (256) succeeded, then
the link was terminated ~140 ms into service discovery — against the WinRT
peer *and* against the vendor reference server identically.

**False leads first ruled out.** Windows killing the link (disproveable:
the terminate was our own `s_fail_connected`, HCI 0x16 local-host); ATT
Find-By-Type-Value being unsupported by Windows (disproven by the fallback
experiment: full discovery failed the same way).

**Root cause.** NimBLE calls GATT discovery callbacks **once per record
with `error->status == 0`** — the error struct is never NULL
(`ble_gattc_error()` wraps every dispatch; ESP-IDF v5.1
`ble_gattc.c:1585` even asserts `service != NULL || status != 0`) — and
once more at the end with `BLE_HS_EDONE`. The checks

```c
if (error != NULL || svc == NULL) { s_fail_connected(...); }
```

are true on **every successful record** (error is non-NULL), so the first
arriving service record was discarded as a failure and the link torn down.
The probe transcript makes it visible: `CONN: full discovery error (0)`
three times in a row — those were the peer's three services arriving
correctly.

**Fix.** Dispatch on the status, not the pointer — `error->status ==
BLE_HS_EDONE` ends the procedure, `error->status != 0` is a real error,
anything else is a record. Applied at five sites: `s_svc_disc_cb`,
`s_all_svc_disc_cb`, `s_chr_disc_cb`, `s_dsc_disc_cb` (discovery) and
`s_read_cb` (attribute — the poll path had the same inversion and would
have counted every successful read as an error).

**Kept addition.** On a *genuine* by-UUID discovery error or empty result,
the code now falls back to full discovery (`ble_gattc_disc_all_svcs`) and
matches the UUID locally before declaring the peer unusable — defensive
robustness for servers that do not serve filtered queries.

## 3. Defect 2 — `json_encode_conn` merge emitted invalid JSON

**Symptom.** With notifications flowing, re-streamed lines arrived as

```
{"ts":77663,"addr":"C6:BE:6C:58:08:74","src":"conn""v": 0"who": "peer"}
```

— no separators between envelope and payload members. Host consumers that
`json.loads()` each line (the hw suite, H5.1/H5.2/H5.3 tooling) dropped
every line.

**Root cause.** The merge loop appended members verbatim;
`s_conn_append_member` never emits the separating comma and the envelope
ends without one. The wrap path (`,"data":"…"`) was correct — only the
merge path was broken.

**Fix.** Each merged member is preceded by `,`, rolled back when the
member is skipped for an envelope-key collision (`ts`/`addr`/`src`) so no
dangling separator remains.

**Test gap closed.** The host tests asserted *substrings* — every fragment
is present in the invalid output, so the suite passed broken lines. The
three merge tests now assert the **exact output string** (plain merge,
envelope-keys-win, nested values) and fail on any separator regression.

## 4. Known issue (peer-side, not firmware) — C2 direct reconnect

Against the WinRT peer, a direct-address reconnect is unreliable: the
PC's connectable advertisement does not reliably survive a connection
cycle. Observed both as *link up → active → dropped* (`errors: 0`,
remote-side drop) and as *connect never establishing* while identical
auto-by-UUID reconnects work minutes later (C6). `test_ble_conn_hw.py`
now SKIPs C2 with the precise reason when the peer refuses, and the
test peer logs advertisement-status transitions for future diagnosis.
Against real sensors (stable addresses, persistent advertising) C2 is
expected to behave normally.

## 5. Library review (the "are there current libraries?" question)

No third-party code is needed. Everything required ships with the
existing NimBLE host: `ble_gattc_disc_all_svcs` (full discovery),
`ble_gattc_disc_svc_by_uuid`, `ble_uuid_cmp`, and the documented callback
convention itself. The fallback is ~40 lines of glue in `ble_conn.c`.
The WinRT side needs only the stock `winrt-*` packages already in use.

## 6. Verification evidence

- **Host suite:** 22/22 (mingw build), including the three upgraded
  exact-string merge tests.
- **Hardware suite** (`tests/hw/test_ble_conn_hw.py`, transcript
  `hw_conn_out.txt`): **43 passed, 0 failed**; C1 verifies connect →
  discover → CCCD subscribe → notify re-stream with valid merged JSON and
  matching envelope address; C5 verifies a 60-notification burst
  (rx_notify counted, device stays responsive); C2/C6 SKIP with reasons.
- **Raw-line probes** (session transcripts): fallback diagnostics
  confirming the per-record `status == 0` calls; re-stream capture showing
  the pre-fix invalid JSON; direct-reconnect probe showing the peer-side
  advertisement quirk.

## 7. Files changed

| File | Change |
|------|--------|
| `firmware/components/ble/ble_conn.c` | callback dispatch fix ×5; full-discovery fallback; diagnostic printfs; header comment |
| `firmware/components/json_enc/json_encoder.c` | merge separator fix with skip rollback |
| `tests/host/test_ble_conn.c` | exact-string assertions on the three merge tests |
| `tests/hw/test_ble_conn_hw.py` | C1 collection-window fix; C2 informed SKIP + peer adv-status logging; corrected session-probe skip message |
