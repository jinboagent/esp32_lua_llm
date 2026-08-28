# Quickstart — the dongle in 10 minutes

Goal: see the whole product loop with your own eyes — scan, filter with
Lua, watch an LLM analyze the traffic and deploy a script back, and see
the connection plane stream real GATT data. Hardware needed: the
ESP32-S3 dongle on COM12 (default), Python 3.10+ with `pyserial`, and a
Windows PC (the GATT peer and WinRT suites are Windows-only; everything
else works anywhere).

Full details live in the root `README.md`; this page is the path.

## 0. One-time setup (~3 min)

```bat
pip install pyserial
scripts\build.bat          :: firmware (already built? skip)
scripts\flash.bat          :: flash the dongle on COM12
```

For the LLM steps: copy your credentials into `.llm_env` in the repo
root (gitignored). Any OpenAI-compatible endpoint works:

```
# either the classic triple...
LLM_BASE_URL=https://api.openai.com/v1
LLM_API_KEY=sk-...
LLM_MODEL=gpt-4o-mini
# ...or provider pairs (DASHSCOPE_* / TOKEN_PLAN_*), model via QWEN_MODEL
```

Notes: real environment variables win as a *unit* (if your shell
exports an `LLM_API_KEY`, unset it or it overrides the file); on Aliyun
MaaS endpoints use the `/compatible-mode/v1` path — the tools even
self-heal this if you paste `/api/v1`. No key at all? Steps 3–5 have
`--no-llm`/`--dry-run` modes so you can still see the mechanics.

## 1. Watch the raw stream (~1 min)

```
scripts\monitor.bat        :: or: putty -serial COM12 -sercfg 115200
```

Type `SCAN START` and watch JSON lines flow — one per BLE
advertisement around you. Try `STATUS`, `SCAN STOP`, `VERSION`.
(Closing the terminal resets the chip; that's expected — N3.)

## 2. Filter by hand with Lua (~2 min)

Still in the console:

```
SCRIPT LOAD
function on_adv(addr, t, rssi, name, uuids, mid, mdata)
    return rssi >= -75 and name ~= nil
end
SCRIPT END
SCRIPT RUN
```

Only named, nearby devices now stream. `SCRIPT STOP` restores the raw
feed. The sandbox rejects `os.`/`io.`/`require` etc. with `-612` — try
it.

## 3. Let an LLM write the filter (~2 min)

One-shot loop (H5.1):

```bat
python llm_loop.py COM12 loop --secs 8 --goal "keep only Apple devices, compact output"
```

Or talk to it instead (H5.3):

```bat
python host_app\assistant.py COM12
```

In the assistant: `/scan on`, then ask *"which devices around me are
Apple?"*, or something ambiguous like *"keep only my two personal
devices"* — the LLM will ask **you** which ones (a `clarify`), then
offer a Lua artifact behind a `deploy? [y/N]` gate. Bring your own
steering text with `--system-extra my_rules.txt`.

## 4. See the connection plane with ground truth (~2 min)

The plant demo (H5.2): your PC becomes a BLE sensor, the dongle
connects and re-streams it, and the LLM estimates the plant's time
constant — checked against the value you configured:

```bat
python host_app\run_case.py COM12 --case first_order --tau 10 --step-at 5 --secs 60 --estimate
```

Expected: ~60/60 `"src":"conn"` lines, the physics checkpoint
y(15 s) ≈ 0.63 green, and `tau_est` within ±30 % of 10.

## 5. Trust but verify (~1 min)

```bat
python tests\host\test_assistant.py     :: 42 unit tests
python tests\host\test_run_case.py      :: 33 unit tests
python tests\hw\test_ble_conn_hw.py     :: on-device suite (needs COM12)
```

Evidence transcripts for every claim above live in
`harness/02-knowledge/`.

## Where to go next

- **Understand the design** — README *Architecture* onward, then
  `harness/01-features/stage5-host/README.md` for the host-tooling map.
- **Change something** — read `CLAUDE.md` (rules + build recipes) and
  `harness/00-global-context/git_workflow.md` (branch → verify →
  process report → squash-merge; never commit to master directly).
- **Add a data case** — one subclass + one line in `CASES` inside
  `host_app/run_case.py`; the runner never changes.
- **Why was X built this way?** — `harness/02-knowledge/` holds a
  process report per session, with transcripts.
