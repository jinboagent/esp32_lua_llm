# LATEST — Status Pointer

**Current status:** [`status-2026-08-16-0105.md`](status-2026-08-16-0105.md)

## At a glance

- **2026-08-15 architecture eval responded** (`docs/evaluation-response-2026-08-16.md`): agreed items fixed, disagreements argued
- **Lua pool rewritten** (`lua_pool.c`): uint32 offsets, on-free coalescing, top-shrink, single ledger — the eval's only correctness bug; 10 new host tests (86/86 total)
- **Lock decoupling**: filter engine owns its mutex; hook presence cached — hot path takes no Lua locks
- **Specs reconciled**: stale `tests/harness/` refs replaced; Implementation Notes in F2.1/F2.2/F2.3/F3.1/F3.2/F4.2; reuse policy annotated
- **tmux `esp32` build pane productized**: `dev_env.bat` + SessionStart hook; shim-kill gotcha documented
- **Verification:** host **86/86** · `test_bridge_hw.py` **32/32** · `test_power_hw.py` **14/14** · `test_ble_lua_hw.py` **45/45** (all on the new firmware)
- **Note (kept):** closing the COM port resets the chip (N3) — suites and soak keep the port open
- **Next:** optional 2 h re-soak of the new allocator; then LLM-loop field test
