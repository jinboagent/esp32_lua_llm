"""Host application-case runner (H5.2) — the plant demo.

Runs a data-generator "case" against the dongle's connection plane: the
PC simulates a plant, streams it over BLE GATT as a WinRT GATT server,
the dongle auto-connects by service UUID and re-streams the values as
"src":"conn" JSON lines, which are collected into a JSONL capture. With
--estimate the capture goes to the cloud LLM, whose answer is checked
against the configured ground truth (not vibes).

  PC plant (this file)                     dongle (unchanged firmware)
  chart-id: CH-runcase-py-01 rev1
  ┌──────────────────┐  GATT notify        ┌──────────────────────────┐
  │ y += (T/τ)(Ku−y) │ ──────────────────▶ │ ble_conn: subscribe,     │
  │ payload {"t","u","y"}                  │ json_encode_conn (merge) │
  └──────────────────┘                     └────────────┬─────────────┘
     ▲ CONN TARGET/START (auto) ────────────────────────┘  USB CDC
     └── "src":"conn" lines ──▶ capture JSONL ──▶ LLM (optional --estimate)

Cases are pluggable: one class + one registry line (CASES below) — the
runner lifecycle never changes. v1 ships `first_order` (notify path);
`read_only` (dongle poll path) is the designated next case.

Runner lifecycle: preflight stop -> peer up (with diagnostics) ->
CONN TARGET (BEFORE SCAN START: the two-UUID TARGET form gets no
response during an active scan — observed 2026-08-28) -> optional
SCAN START -> CONN START -> CONN STATUS to `active` (<=15 s) ->
wait_subscribed -> single-threaded tick loop -> case.check() ->
optional LLM act -> cleanup (CONN STOP [+ SCAN STOP]).

Outcome policy (spec 6.2): a run with the GATT data path available is
scored normally (exit 0 pass / 1 fail). A machine that cannot serve the
GATT-server role reports the precise peer exception + remediation hints
and exits 3; a session that never becomes active is a pass-with-artifact
(exit 0, transcript saved) — a restricted WinRT role is never a product
failure.

The port stays open for the whole run (N3). LLM credentials resolve as
a unit (env vars win as a group, else .llm_env next to the script or in
the repo root), same rule as H5.3.

Usage:
  python host_app/run_case.py [port] --case first_order \
      [--tau 10] [--k 1] [--step-at 5] [--secs 60] [--interval 1] \
      [--estimate] [--with-scan] [--out plant_capture.jsonl]
"""
import argparse
import asyncio
import json
import os
import sys
import threading
import time
import urllib.error
import urllib.request
import uuid as pyuuid

import serial

BAUD = 115200
SLICE_S = 0.2            # runner tick slice (serial drain granularity)
STATUS_EVERY_S = 10.0    # live status line cadence
SESSION_WAIT_S = 15.0    # CONN STATUS poll budget to reach "active"
NOTIFY_CAP = 253         # payload hard cap (proposal hard rule)
LLM_MAX_SAMPLE_LINES = 60  # cap constant for the --estimate act
LLM_TIMEOUT_S = 180

# The test-documented UUID pair (tests/hw/test_ble_conn_hw.py); the case
# owns its identity — future cases may override these per instance.
SVC_UUID = "12345678-1234-1234-1234-123456789abc"
CHR_UUID = "12345678-1234-1234-1234-123456789a01"


# ---- Case contract (the pluggable pattern) ---------------------------------

class Case:
    """One application case. Subclass, fill in, register in CASES."""
    name = ""
    period_s = 1.0
    read_only = False          # True -> exercises the dongle poll path
    svc_uuid = SVC_UUID        # the case owns its GATT identity
    chr_uuid = CHR_UUID
    llm_task = ""

    def add_args(self, parser):
        pass

    def init(self, args):
        pass

    def step(self, t):
        """Return the next payload bytes to notify at elapsed t seconds."""
        raise NotImplementedError

    def read_value(self, t):
        """Bytes served to GATT reads (read_only cases). Default: the
        latest step payload, so the peer always serves live data."""
        return getattr(self, "last_payload",
                       json.dumps({"t": round(t, 3)}).encode())

    def ground_truth(self):
        return ""

    def check(self, samples):
        """Return [(line, ok)] report rows from the collected lines."""
        return []

    def state(self):
        """Short live-status fragment (shown on the periodic line)."""
        return ""


class FirstOrderCase(Case):
    """y += (dt/tau)*(K*u - y), u steps 0->1 at t=step_at (Euler).
    Ground truth: y(step_at+tau) ~= 63.2% of K; steady state ~= K*u."""
    name = "first_order"
    period_s = 1.0
    llm_task = (
        "The samples come from a first-order plant y' = (K*u - y)/tau "
        "whose input u steps 0->1 at some time. Estimate the time "
        "constant tau and the gain K. Reply with JSON only, no fences: "
        '{"tau_est": <number>, "k_est": <number>, "step_at_est": '
        "<number>, \"method\": \"<how you estimated>\"}")

    def add_args(self, parser):
        parser.add_argument("--tau", type=float, default=10.0)
        parser.add_argument("--k", type=float, default=1.0)
        parser.add_argument("--step-at", dest="step_at", type=float,
                            default=5.0)

    def init(self, args):
        self.tau = max(args.tau, 1e-6)
        self.k = args.k
        self.step_at = args.step_at
        self.y = 0.0
        self.u = 0.0
        self.t_last = None
        self.last_payload = b""

    def step(self, t):
        if self.t_last is None:
            self.t_last = t
        dt = max(t - self.t_last, 0.0)
        self.t_last = t
        self.u = 1.0 if t >= self.step_at else 0.0
        self.y += (dt / self.tau) * (self.k * self.u - self.y)
        payload = {"t": round(t, 3), "u": self.u, "y": round(self.y, 4)}
        self.last_payload = json.dumps(payload,
                                       separators=(",", ":")).encode()
        return self.last_payload

    def ground_truth(self):
        return (f"first-order plant: tau={self.tau}, K={self.k}, "
                f"u steps 0->1 at t={self.step_at}; "
                f"y({self.step_at + self.tau:.1f}) ~= "
                f"{0.632 * self.k:.3f} (63.2% of K); "
                f"steady state ~= {self.k:.3f}")

    def check(self, samples):
        rows = []
        rows.append(("envelope ts/addr/src=conn on every line",
                     bool(samples) and all(
                         s.get("ts") is not None and s.get("addr")
                         and s.get("src") == "conn" for s in samples)))
        rows.append(("payload fields t/u/y merged on every line",
                     bool(samples) and all(
                         ("t" in s and "u" in s and "y" in s)
                         for s in samples)))
        pts = [s for s in samples
               if isinstance(s.get("y"), (int, float))
               and isinstance(s.get("t"), (int, float))]
        target_t = self.step_at + self.tau
        if pts:
            near = min(pts, key=lambda s: abs(s["t"] - target_t))
            expected = 0.632 * self.k
            ok = abs(near["y"] - expected) <= 0.10 * max(expected, 0.1)
            rows.append((f"physics: y(t={near['t']:.1f})={near['y']:.3f} "
                         f"vs 63.2%K={expected:.3f} (10% tol)", ok))
            last = max(pts, key=lambda s: s["t"])
            ok_ss = abs(last["y"] - self.k) <= 0.10 * max(abs(self.k), 0.1)
            rows.append((f"steady state: y(t={last['t']:.1f})="
                         f"{last['y']:.3f} vs K={self.k:.3f} (10% tol)",
                         ok_ss))
        else:
            rows.append(("physics checkpoints (no numeric samples)", False))
        return rows

    def state(self):
        return f"u={self.u:.0f} y={self.y:.3f}"


class FirstOrderPollCase(FirstOrderCase):
    """The designated second case (H5.2 open question 4): identical
    plant and ground truth, but read_only — the dongle POLLS the
    characteristic on its CONN INTERVAL instead of subscribing to
    notifications. Same physics checks apply to the polled lines, so
    the poll path is held to the same standard as notify."""
    name = "first_order_poll"
    read_only = True


CASES = {"first_order": FirstOrderCase,          # append-only registry
         "first_order_poll": FirstOrderPollCase}


# ---- PC GATT peer (adapted copy of tests/hw/test_ble_conn_hw.py GattPeer) --
# The test file opens COM12 at module level and cannot be imported; the
# copy serves the live plant value on reads instead of a fixed payload.

class GattPeer:
    """One GATT service, one notify+read characteristic, connectable adv.
    asyncio/await pattern on a daemon thread (WinRT rejects synchronous
    driving of its async operations with E_ILLEGAL_METHOD_CALL)."""

    def __init__(self, svc_uuid, chr_uuid, read_provider,
                 with_notify=True):
        self._svc = svc_uuid
        self._chr = chr_uuid
        self._read_provider = read_provider   # callable(t) -> bytes
        self._with_notify = with_notify       # False -> read-only char:
        # the dongle cannot subscribe and falls back to polling on its
        # CONN INTERVAL (the read_only case's data path)
        self.provider = None
        self.char = None
        self._adv = {"status": None}
        self._loop = asyncio.new_event_loop()
        self._thread = threading.Thread(target=self._loop.run_forever,
                                        daemon=True)
        self._thread.start()

    async def _setup(self):
        from winrt.windows.devices.bluetooth import BluetoothError
        from winrt.windows.devices.bluetooth.genericattributeprofile import (
            GattServiceProvider,
            GattLocalCharacteristicParameters,
            GattCharacteristicProperties,
        )
        from winrt.windows.storage.streams import DataWriter
        ok = int(BluetoothError.SUCCESS)
        res = await GattServiceProvider.create_async(
            pyuuid.UUID(self._svc))
        self.provider = res.service_provider
        if self.provider is None or int(res.error) != ok:
            raise RuntimeError(f"service create failed: {res.error}")

        params = GattLocalCharacteristicParameters()
        params.characteristic_properties = (
            (GattCharacteristicProperties.NOTIFY
             | GattCharacteristicProperties.READ)
            if self._with_notify
            else GattCharacteristicProperties.READ)
        cres = await self.provider.service.create_characteristic_async(
            pyuuid.UUID(self._chr), params)
        self.char = cres.characteristic
        if self.char is None:
            raise RuntimeError(
                f"characteristic create failed: {cres.error}")

        async def handle_read(args):
            req = await args.get_request_async()
            w = DataWriter()
            w.write_bytes(self._read_provider(time.monotonic() - t0))
            req.respond_with_value(w.detach_buffer())

        def on_read(sender, args):
            d = args.get_deferral()

            def done(f):
                try:
                    f.result()
                except Exception:
                    pass
                finally:
                    d.complete()
            asyncio.run_coroutine_threadsafe(
                handle_read(args), self._loop).add_done_callback(done)

        self._on_read = on_read          # keep a strong reference
        self.char.add_read_requested(on_read)

    async def _advertise(self):
        from winrt.windows.devices.bluetooth.genericattributeprofile import (
            GattServiceProviderAdvertisingParameters,
            GattServiceProviderAdvertisementStatus,
        )
        adv_status = GattServiceProviderAdvertisementStatus

        def on_status(sender, args):
            self._adv["status"] = int(args.status)
            print(f"  [peer] advertisement status -> {int(args.status)}",
                  flush=True)
        self._on_status = on_status
        self.provider.add_advertisement_status_changed(on_status)

        adv = GattServiceProviderAdvertisingParameters()
        adv.is_connectable = True
        adv.is_discoverable = True
        self.provider.start_advertising_with_parameters(adv)

        started = {int(adv_status.STARTED),
                   int(adv_status.STARTED_WITHOUT_ALL_ADVERTISEMENT_DATA)}
        end = time.monotonic() + 10
        while time.monotonic() < end:
            if self._adv["status"] in started:
                return
            await asyncio.sleep(0.05)
        raise RuntimeError("advertisement never reached STARTED "
                           f"(status={self._adv['status']})")

    async def _notify(self, payload):
        from winrt.windows.storage.streams import DataWriter
        w = DataWriter()
        w.write_bytes(payload)
        await self.char.notify_value_async(w.detach_buffer())

    async def _subscribed(self):
        try:
            return len(list(self.char.subscribed_clients))
        except Exception:
            return 0

    def start(self):
        asyncio.run_coroutine_threadsafe(
            self._setup(), self._loop).result(15)
        asyncio.run_coroutine_threadsafe(
            self._advertise(), self._loop).result(15)

    def wait_subscribed(self, secs):
        end = time.time() + secs
        while time.time() < end:
            n = asyncio.run_coroutine_threadsafe(
                self._subscribed(), self._loop).result(2)
            if n > 0:
                return True
            time.sleep(0.2)
        return False

    def notify(self, payload):
        asyncio.run_coroutine_threadsafe(
            self._notify(payload), self._loop).result(5)

    def stop(self):
        if self.provider is not None:
            try:
                self._loop.call_soon_threadsafe(
                    self.provider.stop_advertising)
            except Exception:
                pass
        self._loop.call_soon_threadsafe(self._loop.stop)


t0 = time.monotonic()   # read_provider clock base (module import time is
                        # re-based by the runner before the peer starts)


# ---- Serial + LLM helpers (house conventions, self-contained copies) -------

def expected_cmd(line):
    """Response 'cmd' field for a CLI command line ('CONN TARGET x' ->
    'conn_target'); None when unknown. cmd_json matches responses by
    this so a stale status line from a previous exchange can never
    satisfy the wrong command (2026-08-28 stale-line lesson)."""
    words = line.split()
    if len(words) >= 2 and words[0] in ("SCAN", "CONN", "SCRIPT", "POWER"):
        return (words[0] + "_" + words[1]).lower()
    if words and words[0] in ("STATUS", "VERSION"):
        return words[0].lower()
    return None


def cmd_json(s, line, timeout=4.0):
    """Send one CLI command; return the parsed JSON status response,
    matched by its cmd field to the command issued."""
    want = expected_cmd(line)
    s.reset_input_buffer()
    s.write((line + "\n").encode())
    s.flush()
    end = time.time() + timeout
    while time.time() < end:
        raw = s.readline()
        if not raw:
            continue
        txt = raw.decode(errors="replace").strip()
        if txt.startswith("{") and '"status"' in txt:
            try:
                obj = json.loads(txt)
            except json.JSONDecodeError:
                continue
            if isinstance(obj, dict) and "status" in obj and (
                    want is None or obj.get("cmd") == want):
                return obj
    return None


def load_env_file():
    env = {}
    here = os.path.dirname(os.path.abspath(__file__))
    for path in (os.path.join(here, ".llm_env"),
                 os.path.join(os.path.dirname(here), ".llm_env")):
        if os.path.exists(path):
            with open(path, encoding="utf-8") as f:
                for line in f:
                    line = line.strip()
                    if not line or line.startswith("#") or "=" not in line:
                        continue
                    k, v = line.split("=", 1)
                    env[k.strip()] = v.strip().strip('"')
            break
    return env


def resolve_llm_config(force_file=False):
    """Env vars win as a UNIT; else the .llm_env file as-is. The file may
    carry the legacy LLM_* triple OR provider pairs (DASHSCOPE_*,
    TOKEN_PLAN_* — resolved in that order, first complete pair wins);
    the model is LLM_MODEL or QWEN_MODEL. A foreign provider's env key
    never mixes with the file's base URL. force_file=True skips the
    environment entirely (used by the 401 fallback)."""
    file_env = load_env_file()
    env = os.environ
    if not force_file and (
            env.get("LLM_BASE_URL") or env.get("LLM_API_KEY")
            or env.get("OPENAI_API_KEY") or env.get("LLM_MODEL")):
        src, base = env, env.get("LLM_BASE_URL")
        key = src.get("LLM_API_KEY") or src.get("OPENAI_API_KEY") or ""
    else:
        src = file_env
        key = file_env.get("LLM_API_KEY") \
            or file_env.get("OPENAI_API_KEY") or ""
        base = file_env.get("LLM_BASE_URL")
        if not key:
            for prefix in ("DASHSCOPE", "TOKEN_PLAN"):
                b = file_env.get(prefix + "_BASE_URL")
                k = file_env.get(prefix + "_API_KEY")
                if b and k:
                    base, key = b, k
                    break
    if not key:
        return None
    model = (src.get("LLM_MODEL") or file_env.get("QWEN_MODEL")
             or "gpt-4o-mini")
    return {
        "base": (base or "https://api.openai.com/v1").rstrip("/"),
        "key": key,
        "model": model,
        # honor QWEN_ENABLE_THINKING=false from the file: thinking
        # models otherwise burn minutes on tool prompts (observed
        # 2026-08-29: qwen3.8-27b timed out at 180 s with thinking on)
        "no_thinking": (os.environ.get("QWEN_ENABLE_THINKING")
                        or file_env.get("QWEN_ENABLE_THINKING",
                                       "")).lower() == "false",
    }


def env_config_active():
    """True when any LLM_* variable is set in the real environment
    (i.e. the unit rule will prefer the environment over .llm_env)."""
    env = os.environ
    return bool(env.get("LLM_BASE_URL") or env.get("LLM_API_KEY")
                or env.get("OPENAI_API_KEY") or env.get("LLM_MODEL"))


def llm_estimate_resilient(cfg, case, samples):
    """llm_estimate with a bounded 401 fallback: an ambient shell key
    (env-wins-as-unit) can hijack the config to the wrong provider —
    observed twice on this machine. On a 401 with an env-sourced
    config, switch ONCE to the .llm_env config when its key differs;
    the switch is announced. Returns (cfg_used, reply)."""
    try:
        return cfg, llm_estimate(cfg, case, samples)
    except RuntimeError as e:
        if "HTTP 401" not in str(e) or not env_config_active():
            raise
        file_cfg = resolve_llm_config(force_file=True)
        if not file_cfg or file_cfg["key"] == cfg["key"]:
            raise
        print("(llm key from the shell environment was rejected (401) — "
              f"switching to .llm_env [{file_cfg['model']}])")
        return file_cfg, llm_estimate(file_cfg, case, samples)


def strip_fence(text):
    text = text.strip()
    if text.startswith("```"):
        text = text.split("\n", 1)[1] if "\n" in text else ""
        text = text.rsplit("```", 1)[0]
    return text.strip()


def parse_estimate(reply):
    """Parse the LLM estimate reply -> dict | None (defensive)."""
    try:
        obj = json.loads(strip_fence(reply))
    except (json.JSONDecodeError, ValueError):
        return None
    return obj if isinstance(obj, dict) else None


def _estimate_once(cfg, case, samples):
    """One raw estimate request; never heals."""
    body = {
        "model": cfg["model"],
        "temperature": 0.2,
        "messages": [
            {"role": "system", "content":
             "You analyze sensor data and reply with JSON only, no "
             "markdown fences."},
            {"role": "user", "content":
             case.llm_task + "\n\nCaptured samples (each line: dongle "
             "envelope with the plant payload merged; t = seconds):\n"
             + "\n".join(json.dumps(x, separators=(",", ":"))
                         for x in samples[:LLM_MAX_SAMPLE_LINES])},
        ],
    }
    if cfg.get("no_thinking"):
        body["enable_thinking"] = False
    body = json.dumps(body).encode()
    req = urllib.request.Request(
        cfg["base"] + "/chat/completions", data=body,
        headers={"Content-Type": "application/json",
                 "Authorization": "Bearer " + cfg["key"]})
    try:
        with urllib.request.urlopen(req, timeout=LLM_TIMEOUT_S) as resp:
            out = json.load(resp)
    except urllib.error.HTTPError as e:
        raise RuntimeError(f"LLM HTTP {e.code}: "
                           f"{e.read()[:300].decode(errors='replace')}")
    except urllib.error.URLError as e:
        raise RuntimeError(f"LLM unreachable: {e.reason}")
    except TimeoutError:
        raise RuntimeError("LLM timeout: no response within the limit")
    return out["choices"][0]["message"]["content"]


def llm_estimate(cfg, case, samples):
    """Estimate act with the /api/v1 -> /compatible-mode/v1 self-heal:
    a 404 from a base ending /api/v1 means the native DashScope-dialect
    root was configured (the Aliyun console hands that path out); the
    OpenAI-compatible route on the same host is /compatible-mode/v1.
    Retries once, prints the switch, heals cfg for the session (same
    rule as the assistant's llm_chat; observed twice 2026-08-28)."""
    try:
        return _estimate_once(cfg, case, samples)
    except RuntimeError as e:
        if "HTTP 404" not in str(e) or "/api/v1" not in cfg["base"]:
            raise
        healed = cfg["base"].rsplit("/api/v1", 1)[0] + "/compatible-mode/v1"
        print(f"(base {cfg['base']} answered 404 (native dialect root) "
              f"— retrying on {healed})")
        cfg["base"] = healed
        return _estimate_once(cfg, case, samples)


# ---- Runner ----------------------------------------------------------------

PEER_HINTS = """peer startup failed — this machine cannot currently serve
the WinRT GATT-server role. Checks, in order:
  1. Bluetooth is on (Settings > Bluetooth & devices).
  2. The adapter supports the LE GATT-server role (many USB dongles and
     some laptop radios do not); try the built-in radio.
  3. No other process holds the GATT server (restart helps).
  4. Unpackaged desktop apps may hit the GattServiceProvider
     package-identity restriction on some Windows builds.
This is an environment limitation, not a dongle failure."""


def run(args):
    global t0
    case = CASES[args.case]()
    case.init(args)
    expected = int(args.secs / args.interval)
    print(f"=== run_case {case.name} on {args.port} ===")
    print(f"ground truth: {case.ground_truth()}")
    print(f"expect ~{expected} conn lines over {args.secs:.0f}s "
          f"@ {args.interval:.2f}s cadence"
          + ("  (+ adv plane)" if args.with_scan else ""))

    try:
        s = serial.Serial(args.port, BAUD, timeout=1)
        time.sleep(0.5)
        s.reset_input_buffer()
    except serial.SerialException as e:
        print(f"error: no device on {args.port}: {e}")
        return 2

    samples = []
    peer = None
    scan_on = False
    try:
        # Preflight: leave the device idle before the demo. CONN STOP can
        # return ok while the GAP disconnect event is still in flight, so
        # wait for the state to settle at "off" before TARGET (the F2.4
        # -452 race, observed 2026-08-28).
        cmd_json(s, "CONN STOP", 8.0)
        cmd_json(s, "SCAN STOP", 4.0)
        end = time.time() + 5.0
        while time.time() < end:
            st = cmd_json(s, "CONN STATUS")
            if st and st.get("state") == "off":
                break
            time.sleep(0.2)

        try:
            t0 = time.monotonic()
            peer = GattPeer(case.svc_uuid, case.chr_uuid, case.read_value,
                            with_notify=not case.read_only)
            peer.start()
            time.sleep(1.0)
        except Exception as e:
            print(f"\n{PEER_HINTS}\n\nfull exception:\n{e!r}")
            return 3

        # CONN TARGET BEFORE SCAN START (two-UUID TARGET gets no response
        # during an active scan; the target is still set — set it early).
        r = cmd_json(s, f"CONN TARGET {case.svc_uuid} {case.chr_uuid}")
        if not (r and r.get("status") == "ok"):
            print(f"error: CONN TARGET failed: {r}")
            return 1
        if args.with_scan:
            r = cmd_json(s, "SCAN START")
            scan_on = bool(r and r.get("status") == "ok")
            print(f"SCAN START: {'ok' if scan_on else r}")

        r = cmd_json(s, "CONN START", 15.0)
        if not (r and r.get("status") == "ok"):
            print(f"error: CONN START failed: {r}")
            return 1

        state = None
        end = time.time() + SESSION_WAIT_S
        while time.time() < end:
            state = cmd_json(s, "CONN STATUS")
            if state and state.get("state") == "active":
                break
            time.sleep(0.5)
        if not (state and state.get("state") == "active"):
            # Pass-with-artifact (spec 6.2b): the precise status is the
            # deliverable; a restricted session role is not a failure.
            print("\nsession never became active — pass-with-artifact. "
                  "final CONN STATUS:")
            print(json.dumps(state, indent=2))
            print("see the CONN: diagnostics in a raw-line transcript; "
                  "ble_conn.c s_fail_connected terminates the link on "
                  "discovery/subscribe failure by design")
            return 0
        print(f"connected to peer {state.get('addr')} "
              f"(mode {state.get('mode')})")
        if case.read_only:
            # Poll path: the dongle reads the characteristic on its own
            # CONN INTERVAL; no CCCD subscription is needed. The peer's
            # read handler serves case.read_value(t) live.
            r = cmd_json(s, f"CONN INTERVAL "
                        f"{int(case.period_s * 1000)}")
            print(f"poll mode: CONN INTERVAL -> "
                  f"{r.get('value') if r else r} ms"
                  if r and r.get("status") == "ok"
                  else f"poll mode: CONN INTERVAL failed: {r}")
        elif not peer.wait_subscribed(8):
            print("warning: subscription not confirmed within 8s "
                  "(continuing; notify may still flow)")
        else:
            print("peer subscribed (notify path ready)")

        # Single-threaded tick loop: drain in slices, notify on cadence.
        s.timeout = SLICE_S          # bound each drain read by the slice
        s.reset_input_buffer()
        with open(args.out, "w", encoding="utf-8") as f:
            start = time.monotonic()
            next_tick = 0.0          # elapsed-time boundaries (relative,
            next_status = STATUS_EVERY_S  # like t itself — never mix in
            n_notify = 0             # absolute monotonic stamps here)
            while True:
                t = time.monotonic() - start
                if t >= args.secs:
                    break
                if t >= next_tick:
                    payload = case.step(t)
                    if len(payload) > NOTIFY_CAP:
                        raise RuntimeError(
                            f"payload {len(payload)}B exceeds the "
                            f"{NOTIFY_CAP}B notify cap")
                    if not case.read_only:
                        peer.notify(payload)
                    # read_only cases still step the plant each tick;
                    # the dongle pulls the value via GATT reads
                    n_notify += 1
                    next_tick += args.interval
                # drain one slice
                slice_end = time.monotonic() + SLICE_S
                while time.monotonic() < slice_end:
                    raw = s.readline()
                    if raw:
                        txt = raw.decode(errors="replace").strip()
                        if txt.startswith("{") and '"src":"conn"' in txt:
                            try:
                                obj = json.loads(txt)
                                samples.append(obj)
                                f.write(json.dumps(obj,
                                                   separators=(",", ":"))
                                        + "\n")
                            except json.JSONDecodeError:
                                pass
                    elif time.monotonic() >= slice_end:
                        break
                if t >= next_status:
                    print(f"t={t:5.1f}s  {case.state():<14} "
                          f"lines {len(samples)}/{n_notify}")
                    next_status += STATUS_EVERY_S

        # Summary
        ratio = len(samples) / n_notify if n_notify else 0.0
        print(f"\n--- summary: {len(samples)} / {n_notify} notified lines "
              f"received ({ratio:.0%}) -> "
              f"{'PASS' if ratio >= 0.90 else 'FAIL'} (>=90% required)")
        print(f"capture: {args.out}")
        print(f"ground truth: {case.ground_truth()}")
        failed = ratio < 0.90
        if samples:
            for line, ok in case.check(samples):
                print(f"  {'PASS' if ok else 'FAIL'}  {line}")
                failed = failed or not ok
        else:
            print("  FAIL  no conn lines collected")
            failed = True

        if args.estimate:
            cfg = resolve_llm_config()
            if not samples:
                print("\n--estimate skipped: no conn lines collected")
            elif cfg is None:
                print("\n--estimate skipped: no LLM key configured "
                      "(set LLM_API_KEY or .llm_env)")
            else:
                print(f"\n--- estimate act (LLM: {cfg['model']}, "
                      f"{min(len(samples), LLM_MAX_SAMPLE_LINES)} lines)")
                try:
                    _, reply = llm_estimate_resilient(cfg, case, samples)
                    est = parse_estimate(reply)
                    if est is None:
                        print("  FAIL  LLM reply not parseable JSON: "
                              f"{strip_fence(reply)[:200]}")
                        failed = True
                    else:
                        tau_e, k_e = est.get("tau_est"), est.get("k_est")
                        print(f"  configured tau={case.tau} K={case.k}")
                        print(f"  estimated   tau={tau_e} K={k_e} "
                              f"(method: {est.get('method', '?')})")
                        try:
                            within = abs(float(tau_e) - case.tau) \
                                <= 0.30 * case.tau
                            print(f"  {'PASS' if within else 'FAIL'}  "
                                  "tau_est within ±30% of configured")
                            failed = failed or not within
                        except (TypeError, ValueError):
                            print("  FAIL  tau_est not numeric")
                            failed = True
                except RuntimeError as e:
                    print(f"  estimate act failed: {e}")
                    failed = True
        return 1 if failed else 0
    except KeyboardInterrupt:
        print("\n(interrupted — cleaning up)")
        return 0
    finally:
        try:
            cmd_json(s, "CONN STOP", 8.0)
            if scan_on:
                cmd_json(s, "SCAN STOP", 4.0)
        finally:
            if peer is not None:
                peer.stop()
            s.close()          # N3: the final close at orderly exit only
            print("(cleanup done: CONN STOP"
                  + (" + SCAN STOP" if scan_on else "")
                  + "; port closed)")


def main():
    # Pre-parse --case with help disabled so the case-specific flags can
    # join the real parser BEFORE it handles --help (two-stage parse).
    pre = argparse.ArgumentParser(add_help=False)
    pre.add_argument("--case", default="first_order",
                     choices=sorted(CASES))
    case = CASES[pre.parse_known_args()[0].case]()
    p = argparse.ArgumentParser(
        description="Host application-case runner for the BLE bridge "
                    "dongle (spec: harness/01-features/stage5-host/).")
    p.add_argument("port", nargs="?", default="COM12",
                   help="serial port (default COM12)")
    p.add_argument("--case", default="first_order",
                   choices=sorted(CASES), help="application case to run")
    p.add_argument("--secs", type=float, default=60.0)
    p.add_argument("--interval", type=float, default=None,
                   help="notify cadence in seconds (default: the case "
                        "period)")
    p.add_argument("--with-scan", action="store_true",
                   help="also run the adv plane alongside (counted "
                        "separately; off by default)")
    p.add_argument("--estimate", action="store_true",
                   help="send the capture to the LLM and check its "
                        "estimate against the ground truth")
    p.add_argument("--out", default="plant_capture.jsonl")
    case.add_args(p)               # case-specific flags join the parser
    args = p.parse_args()
    if args.interval is None:
        args.interval = case.period_s
    if args.secs <= 0 or args.interval <= 0:
        # unguarded, --interval 0 is a ZeroDivisionError at the expected-
        # count print and a negative value a notify storm in the tick loop
        p.error("--secs and --interval must be positive")
    sys.exit(run(args))


if __name__ == "__main__":
    main()
