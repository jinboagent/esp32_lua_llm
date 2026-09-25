# Demo — first-order time-constant estimation (with host-side BLE simulation)

> **Author:** zcode · 2026-09-25 · The end-to-end proof that the LLM loop
> works on real hardware: the PC simulates a BLE sensor, the dongle connects
> and re-streams it, and the LLM's answer is graded against physics.
> Overview of the case runner: this page; features: `docs/features.zcode.md`.

## What it demonstrates

1. **Host-side BLE simulation** — your PC becomes a real BLE peripheral
   (a WinRT GATT server), so the demo needs no second device and is perfectly
   reproducible.
2. **The dongle's connection plane** — auto-connect by service UUID,
   subscription (or polling fallback), re-streaming as `"src":"conn"` lines.
3. **A checkable LLM answer** — the model estimates the plant's time constant
   from raw samples; the harness grades it against the value you configured.

## The physics

The simulated sensor is a first-order plant:

```
y' = (K·u − y) / τ          u steps 0 → 1 at t = step_at
```

Ground truth used for grading (independent of the LLM):

- at `t = step_at + τ`, the output must reach **63.2 % of K** (the defining
  property of a first-order step response),
- and the steady state must approach `K`.

Defaults: `--tau 10`, `--k 1.0`, `--step-at 5`, `--secs 60`.

## The host-side simulator (GattPeer)

`host_app/run_case.py` starts a GATT server on the PC's Bluetooth adapter:

| Item | Value |
|------|-------|
| Service UUID | `12345678-1234-1234-1234-123456789abc` |
| Characteristic | `12345678-1234-1234-1234-123456789a01` (notify + read) |
| Payload | `{"t":…, "u":…, "y":…}` — one line per interval (default 1 s) |

The dongle auto-connects by the service UUID and subscribes. In the
**`first_order_poll`** variant the characteristic is created **read-only**, so
subscribing is impossible — the dongle detects this and falls back to polling
reads on its `CONN INTERVAL`. That fallback path is a feature of the firmware,
and this case proves it end-to-end.

## How to run

```bat
:: notify variant (subscription path)
python host_app\run_case.py COM12 --case first_order --tau 10 --step-at 5 --secs 60 --estimate

:: poll variant (read-only peer — dongle must poll)
python host_app\run_case.py COM12 --case first_order_poll --estimate

:: no hardware needed to explore the interface:
python host_app\run_case.py --help
```

| Flag | Meaning |
|------|---------|
| `--case first_order \| first_order_poll` | notify vs read-only (polling) peer |
| `--tau/--k/--step-at` | plant parameters (also the ground truth) |
| `--secs/--interval` | capture length and notify cadence |
| `--with-scan` | also run the advertisement plane during the case |
| `--estimate` | send the capture to the LLM and grade its τ estimate (±30 % gate) |
| `--out file.jsonl` | where the capture lands (default `plant_capture.jsonl`) |

## What PASS looks like

```
--- summary: 60 / 60 notified lines received (100%) -> PASS (>=90% required)
PASS envelope ts/addr/src=conn on every line
PASS payload fields t/u/y merged
PASS physics: y(t=15.0)=0.633 vs 63.2%K=0.632 (10% tol)
PASS steady state
PASS tau_est within ±30% of configured
```

Exit codes: `0` pass · `1` fail · `2` no device found · `3` this PC cannot
serve the GATT-server role (Windows + `winrt-*` packages + a Bluetooth adapter
required — see below).

The headline recorded result (live hardware run): **60/60 lines captured, and
the LLM's estimate τ̂ = 9.5 against a configured τ = 10.0** — a −5 % error,
well inside the ±30 % grading gate and the ±10 % physics tolerance.

## Requirements & notes

- Windows with a Bluetooth adapter, `pip install pyserial winrt-runtime`
  (the case runner prints exact remediation if the WinRT role is unavailable).
- The dongle on COM12 (or pass another port) — one program owns the port at a
  time; close PuTTY/monitor first.
- Keep the COM port open for the whole run — closing it resets the chip (N3).
- The capture JSONL and the transcript stay next to where you ran the command —
  attach them to bug reports.
