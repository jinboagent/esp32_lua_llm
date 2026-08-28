# Testing — inventory and how to run it

Four layers; everything hardware-facing runs against the real dongle
(COM12 by default — one program owns the port at a time). The
post-merge regression gate is one command:

```bash
python tests/hw/run_all_hw.py            # whole battery, tees transcripts
python tests/hw/run_all_hw.py --quick    # conn + power subset
python tests/hw/run_all_hw.py --list     # what it would run
```

| Layer | Where | What it covers | Run it |
|-------|-------|----------------|--------|
| Host unit (C, Unity) | `tests/host/*.c` | AD parser, JSON encoders (exact bytes + fuzz round-trips), filter engine, CLI state machine, **response contract** (every command family, ok + error paths, strict-JSON validated), bridge protocol incl. the sandbox scanner corpus, Lua pool | `cmake -S tests/host -B <dir> -G Ninja && cmake --build <dir> && <dir>/test_runner.exe` |
| Host unit (python) | `tests/host/test_assistant.py`, `test_run_case.py` | typed-envelope parsing, buffers/history, deploy gate + mid-upload −612, plant physics vs ground truth, LLM config + self-heals, **golden-transcript classification** (real device noise), **whole REPL sessions** over a device simulator | `python tests/host/test_assistant.py` |
| HW suites | `tests/hw/test_*_hw.py` | bridge 32 · power 14 · BLE+Lua 45 · peer 11 · conn **65** (C0 control plane, C1–C6 GATT tier, **C7 state matrix**) | individually, or via `run_all_hw.py` |
| Soaks | `tests/hw/soak_test.py` (adv/Lua pool), `tests/hw/soak_conn.py` (conn plane) | hours-long stability: counters, heap/pool sampling, reconnect cycles | `python tests/hw/soak_conn.py --secs 3600 --reconnect-every 300` |

## The layers that were added 2026-08-29, and why

Each new layer closes a gap a real bug walked through:

- **Response contract** (`test_cli_responses.c`): the 2026-08-28
  `CONN TARGET` bug shipped invalid JSON for weeks because nothing
  validated response bytes. Now every documented response must be
  strict JSON with the right `cmd` field — 76 responses per run.
- **Fuzz + boundaries** (`test_fuzz.c`): deterministic LCG inputs
  through both encoders with a strict-JSON oracle; every small buffer
  size probed; the bridge scanner's fail-closed policy pinned by a
  corpus. Found 3 real encoder defects on its first run (2-digit
  `\u` escapes, invalid escapes copied through the merge path,
  dangling-quote/trunc output at tight caps) — all fixed.
- **Golden transcripts** (`fixtures/device_stream_sample.txt`): frozen
  real-world line shapes (ANSI NimBLE logs, CLI echoes, boot logs,
  garbage, the historical malformed response) — the classification
  layer never sees these in synthetic tests. Tripped a real
  stale-line weakness in the host tools' `cmd_json`, now hardened.
- **REPL-loop tests**: whole sessions over a device simulator — the
  loop layer where the H5.2 tick-units bug lived was previously
  untested.
- **C7 state matrix** (`test_ble_conn_hw.py`): walks
  idle → scanning → loaded → running and asserts every response AND
  the observed state (`STATUS`/`CONN STATUS`), not just return codes —
  "STOP returned ok" is not "state is off" (the 2026-08-28 race).
- **soak_conn.py**: the conn-plane equivalent of the H4 adv soak —
  covers what C6's supervision SKIP can't (long sessions, reconnect
  cycles, counter drift).
- **run_all_hw.py**: asserts firmware VERSION first, tees every
  transcript to `harness/02-knowledge/evidence-hw-runs/<timestamp>/`,
  and gates on the chip's reset reason after each suite — a suite
  that crashes the device fails loudly.

## Conventions

- **Evidence**: every verification claim references a transcript under
  `harness/02-knowledge/` — nothing is "verified" without one.
- **Strict JSON everywhere**: `json.loads`-equivalent assertions on
  every response; substring matching let H1 slip through.
- **Response↔command matching**: readers match the `cmd` field (hw
  suite, assistant, run_case, soak) — a stale status line must never
  satisfy the wrong exchange.
- **Deterministic seeds**: fuzz tests print their seed on failure and
  always reproduce.
- Historical status files (status/status-*.md) are append-only records;
  current state lives in `status/LATEST.md`.
