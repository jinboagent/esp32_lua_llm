# LATEST — Status Pointer

**Current status:** [`status-2026-08-11-2156.md`](status-2026-08-11-2156.md)

## At a glance

- **Firmware v1.0.0 + N1/N2 + H1/H2/H3 fixes** — flashed and verified on COM12 this session
- **Eval→fix cycle complete**: H1 (JSON escaping of Lua error/result text), H2 (overlong-line drain), H3 (121-byte chunk cap documented) — all fixed, all regression-covered; H4 (pool fragmentation) stays a soak-test watch item
- **Bonus fix**: host suite restored to buildable (`tests/host/esp_system.h` shim — it had been broken since the N1/N2 changes)
- **Verification:** host **76/76** · `test_ble_lua_hw.py` **45/45** · `test_ble_peer_hw.py` **11/11** · `test_bridge_hw.py` **32/32** · `test_power_hw.py` **14/14**
- **Docs:** `bug_check/stage-all-eval-2026-08-11.md` (evaluation, now marked resolved) + `bug_check/bug_fix_report/fix-report-2026-08-11-eval-h1-h3.md` (fix details)
- **Note (kept):** closing the COM port resets the chip (N3) — suites keep the port open
- **Environment:** bleak 3.0.2 + winrt 3.2.1; Win10 BLE advertising rejects `local_name` payloads — peer tests key on manufacturer data
- **Next:** commit the session (suggested message in the report), add heap/Lua-pool metrics to STATUS, then the 2–4 h soak and stress battery; after that the full LLM-loop field test
