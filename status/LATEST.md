# LATEST — Status Pointer

**Current status:** [`status-2026-08-11-2225.md`](status-2026-08-11-2225.md)

## At a glance

- **Firmware v1.0.0 + N1/N2 + H1/H2/H3 + H4 metrics** — flashed and verified on COM12
- **H4 observability landed**: STATUS now reports `free_heap` + `lua_pool{used,peak}` (`lua_engine_pool_stats()` API); host 76/76 with new assertions
- **2 h soak CLOSED 2026-08-12**: no fragmentation (lua_used early/late avg 20846/21738, troughs flat), no stalls, no resets; `lua_peak` fixed to a true high-water mark; soak_test.py criterion now trend-based
- **Stale T1–T4 scripts deleted** (`9e9a3a9`)
- **Verification:** host **76/76** · `test_ble_lua_hw.py` **45/45** · `test_ble_peer_hw.py` **11/11** · `test_bridge_hw.py` **32/32** · `test_power_hw.py` **14/14**
- **Note (kept):** closing the COM port resets the chip (N3) — suites and soak keep the port open
- **Next:** stress battery (eval §hardware plan) → full LLM-loop field test
