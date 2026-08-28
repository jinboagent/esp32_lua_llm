"""Host unit tests for host_app/run_case.py (H5.2).

Run:  python tests/host/test_run_case.py
Covers the pure case logic: first-order plant integration and physics
checkpoints, payload shape and the notify cap, the check() report rows,
the CASES registry contract, CLI arg binding, and the defensive estimate
parser. The WinRT peer and the serial orchestration are exercised by
the live run (harness/02-knowledge/ evidence).
"""
import argparse
import json
import os
import sys
import unittest
import unittest.mock

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "..", "host_app"))
import run_case  # noqa: E402


def make_case(tau=10.0, k=1.0, step_at=5.0):
    c = run_case.FirstOrderCase()
    c.init(argparse.Namespace(tau=tau, k=k, step_at=step_at))
    return c


def simulate(c, secs, dt=1.0):
    """Drive case.step() like the runner does; return dongle-style
    envelopes (ts/addr/src merged with the plant payload)."""
    out = []
    t = 0.0
    while t <= secs + 1e-9:
        payload = json.loads(c.step(t))
        out.append({"ts": int(t * 1000), "addr": "AA:BB:CC:DD:EE:FF",
                    "src": "conn", **payload})
        t += dt
    return out


class FirstOrderPhysicsTests(unittest.TestCase):
    def test_63p2_percent_checkpoint(self):
        c = make_case(tau=10.0, k=1.0, step_at=5.0)
        lines = simulate(c, 60.0)
        at = min(lines, key=lambda l: abs(l["t"] - 15.0))
        # Euler at dt=1 gives 1-0.9^10 = 65.1% (vs exact 63.2%) - inside
        # the 10% tolerance the check uses
        self.assertLessEqual(abs(at["y"] - 0.632), 0.10 * 0.632)

    def test_steady_state_reaches_k(self):
        c = make_case(tau=10.0, k=2.0, step_at=5.0)
        lines = simulate(c, 60.0)
        last = max(lines, key=lambda l: l["t"])
        self.assertLessEqual(abs(last["y"] - 2.0), 0.10 * 2.0)

    def test_y_zero_before_step(self):
        c = make_case(tau=10.0, k=1.0, step_at=5.0)
        lines = simulate(c, 4.0)
        self.assertTrue(all(l["y"] == 0.0 and l["u"] == 0.0
                            for l in lines))

    def test_first_step_zero_dt(self):
        c = make_case()
        first = json.loads(c.step(0.0))
        self.assertEqual(first, {"t": 0.0, "u": 0.0, "y": 0.0})


class PayloadTests(unittest.TestCase):
    def test_payload_shape_and_compact(self):
        c = make_case()
        raw = c.step(12.345)
        obj = json.loads(raw)
        self.assertEqual(sorted(obj), ["t", "u", "y"])
        self.assertLessEqual(len(raw), 60)  # design note: ~40 bytes

    def test_payload_under_notify_cap(self):
        c = make_case(tau=3.0, k=123.456, step_at=2.0)
        for t in range(0, 40):
            self.assertLessEqual(len(c.step(float(t))),
                                 run_case.NOTIFY_CAP)

    def test_read_value_serves_live_payload(self):
        c = make_case()
        c.step(7.0)
        self.assertEqual(c.read_value(0.0), c.last_payload)


class CheckRowsTests(unittest.TestCase):
    def good_samples(self):
        c = make_case()
        return c, simulate(c, 60.0)

    def test_good_run_all_rows_pass(self):
        c, lines = self.good_samples()
        rows = c.check(lines)
        self.assertGreaterEqual(len(rows), 4)
        for line, ok in rows:
            self.assertTrue(ok, line)

    def test_bad_envelope_flagged(self):
        c, lines = self.good_samples()
        lines[3].pop("addr")
        rows = dict(c.check(lines))
        self.assertFalse(rows["envelope ts/addr/src=conn on every line"])

    def test_missing_payload_flagged(self):
        c, lines = self.good_samples()
        lines[5].pop("y")
        rows = dict(c.check(lines))
        self.assertFalse(rows["payload fields t/u/y merged on every line"])

    def test_empty_samples(self):
        c = make_case()
        rows = dict(c.check([]))
        self.assertFalse(rows["envelope ts/addr/src=conn on every line"])
        self.assertFalse(rows["physics checkpoints (no numeric samples)"])


class RegistryAndArgsTests(unittest.TestCase):
    def test_registry_contains_first_order(self):
        self.assertIn("first_order", run_case.CASES)
        self.assertEqual(run_case.CASES["first_order"].name,
                         "first_order")

    def test_case_flags_join_parser(self):
        c = run_case.CASES["first_order"]()
        p = argparse.ArgumentParser()
        c.add_args(p)
        args = p.parse_args(["--tau", "7.5", "--k", "1.5",
                             "--step-at", "3"])
        c.init(args)
        self.assertEqual((c.tau, c.k, c.step_at), (7.5, 1.5, 3.0))

    def test_contract_defaults(self):
        c = make_case()
        self.assertEqual(c.period_s, 1.0)
        self.assertFalse(c.read_only)
        self.assertTrue(c.llm_task)
        self.assertIn("63.2%", c.ground_truth())

    def test_uuids_match_test_pair(self):
        c = make_case()
        self.assertEqual(c.svc_uuid, run_case.SVC_UUID)
        self.assertEqual(c.chr_uuid, run_case.CHR_UUID)


class ConfigResolutionTests(unittest.TestCase):
    """Env wins as a unit; the file may carry the legacy LLM_* triple or
    provider pairs (DASHSCOPE_* before TOKEN_PLAN_*); model may come
    from QWEN_MODEL."""

    def resolve(self, env, file_env):
        with unittest.mock.patch.dict(os.environ, env, clear=True), \
             unittest.mock.patch.object(run_case, "load_env_file",
                                        return_value=file_env):
            return run_case.resolve_llm_config()

    def test_env_unit_wins(self):
        cfg = self.resolve({"LLM_API_KEY": "sk-env"},
                           {"LLM_BASE_URL": "https://file/v1",
                            "LLM_API_KEY": "sk-file"})
        self.assertEqual(cfg["key"], "sk-env")
        self.assertNotEqual(cfg["base"], "https://file/v1")

    def test_legacy_file_triple(self):
        cfg = self.resolve({}, {"LLM_BASE_URL": "https://legacy/v1",
                                "LLM_API_KEY": "sk-legacy"})
        self.assertEqual((cfg["base"], cfg["key"]),
                         ("https://legacy/v1", "sk-legacy"))

    def test_provider_pairs_dashscope_first(self):
        cfg = self.resolve({}, {
            "DASHSCOPE_BASE_URL": "https://dash/v1",
            "DASHSCOPE_API_KEY": "sk-dash",
            "TOKEN_PLAN_BASE_URL": "https://tp/v1",
            "TOKEN_PLAN_API_KEY": "sk-tp",
            "QWEN_MODEL": "qwen3.8-flash"})
        self.assertEqual((cfg["base"], cfg["key"], cfg["model"]),
                         ("https://dash/v1", "sk-dash", "qwen3.8-flash"))

    def test_provider_pair_fallback_to_token_plan(self):
        cfg = self.resolve({}, {"TOKEN_PLAN_BASE_URL": "https://tp/v1",
                                "TOKEN_PLAN_API_KEY": "sk-tp"})
        self.assertEqual((cfg["base"], cfg["key"]),
                         ("https://tp/v1", "sk-tp"))
        self.assertEqual(cfg["model"], "gpt-4o-mini")  # no model set

    def test_nothing_configured(self):
        self.assertIsNone(self.resolve({}, {}))


class Fallback401Tests(unittest.TestCase):
    """An ambient shell key can hijack the config (env-wins-as-unit) and
    401 against the wrong provider; on 401 with an env-sourced config
    the estimate act switches once to .llm_env."""

    FILE = {"DASHSCOPE_BASE_URL": "https://dash/v1",
            "DASHSCOPE_API_KEY": "sk-file"}
    ENV = {"LLM_API_KEY": "sk-env-expired"}

    def test_force_file_ignores_env(self):
        with unittest.mock.patch.dict(os.environ, self.ENV, clear=True), \
             unittest.mock.patch.object(run_case, "load_env_file",
                                        return_value=dict(self.FILE)):
            cfg = run_case.resolve_llm_config(force_file=True)
        self.assertEqual((cfg["base"], cfg["key"]),
                         ("https://dash/v1", "sk-file"))

    def test_switch_on_401(self):
        case = make_case()
        good = '{"tau_est": 9.5, "k_est": 1.0}'
        with unittest.mock.patch.dict(os.environ, self.ENV, clear=True), \
             unittest.mock.patch.object(run_case, "load_env_file",
                                        return_value=dict(self.FILE)), \
             unittest.mock.patch.object(
                 run_case, "llm_estimate",
                 side_effect=[RuntimeError("LLM HTTP 401: bad"), good]) \
                 as est:
            cfg = {"base": "https://x/v1", "key": "sk-env", "model": "m"}
            used, reply = run_case.llm_estimate_resilient(cfg, case, [])
        self.assertEqual(est.call_count, 2)
        self.assertEqual(used["key"], "sk-file")       # switched
        self.assertEqual(est.call_args_list[1].args[0]["key"], "sk-file")
        self.assertIn("9.5", reply)

    def test_no_switch_on_500(self):
        case = make_case()
        with unittest.mock.patch.dict(os.environ, self.ENV, clear=True), \
             unittest.mock.patch.object(
                 run_case, "llm_estimate",
                 side_effect=RuntimeError("LLM HTTP 500: bad")):
            with self.assertRaises(RuntimeError):
                run_case.llm_estimate_resilient(
                    {"base": "b", "key": "k", "model": "m"}, case, [])

    def test_no_switch_without_env(self):
        case = make_case()
        with unittest.mock.patch.dict(os.environ, {}, clear=True), \
             unittest.mock.patch.object(
                 run_case, "llm_estimate",
                 side_effect=RuntimeError("LLM HTTP 401: bad")):
            with self.assertRaises(RuntimeError):
                run_case.llm_estimate_resilient(
                    {"base": "b", "key": "k", "model": "m"}, case, [])


class BasePathHealTests(unittest.TestCase):
    """A base ending /api/v1 is the native DashScope root; the estimate
    act retries once on the same host's /compatible-mode/v1."""

    def test_heal_on_404(self):
        cfg = {"base": "https://ws-x.maas.aliyuncs.com/api/v1",
               "key": "k", "model": "m"}
        with unittest.mock.patch.object(
                run_case, "_estimate_once",
                side_effect=[RuntimeError("LLM HTTP 404: "),
                             '{"tau_est": 1}']) as once:
            out = run_case.llm_estimate(cfg, make_case(), [])
        self.assertIn("tau_est", out)
        self.assertEqual(once.call_count, 2)
        self.assertEqual(cfg["base"],
                         "https://ws-x.maas.aliyuncs.com"
                         "/compatible-mode/v1")

    def test_no_heal_without_api_v1_base(self):
        cfg = {"base": "https://api.example.com/v1", "key": "k",
               "model": "m"}
        with unittest.mock.patch.object(
                run_case, "_estimate_once",
                side_effect=RuntimeError("LLM HTTP 404: body")):
            with self.assertRaises(RuntimeError):
                run_case.llm_estimate(cfg, make_case(), [])
        self.assertEqual(cfg["base"], "https://api.example.com/v1")

    def test_no_heal_on_500(self):
        cfg = {"base": "https://ws-x.maas.aliyuncs.com/api/v1",
               "key": "k", "model": "m"}
        with unittest.mock.patch.object(
                run_case, "_estimate_once",
                side_effect=RuntimeError("LLM HTTP 500: oops")):
            with self.assertRaises(RuntimeError):
                run_case.llm_estimate(cfg, make_case(), [])


class EstimateParseTests(unittest.TestCase):
    def test_plain_json(self):
        est = run_case.parse_estimate(
            '{"tau_est": 9.2, "k_est": 1.05, "method": "63% point"}')
        self.assertEqual(est["tau_est"], 9.2)

    def test_fenced_json(self):
        est = run_case.parse_estimate(
            '```json\n{"tau_est": 11, "k_est": 0.9}\n```')
        self.assertEqual(est["tau_est"], 11)

    def test_garbage_returns_none(self):
        self.assertIsNone(run_case.parse_estimate("tau is about ten"))
        self.assertIsNone(run_case.parse_estimate("[1,2,3]"))

    def test_llm_task_mentions_json_only(self):
        self.assertIn("JSON only", make_case().llm_task)


class CliGuardTests(unittest.TestCase):
    """--interval 0 used to be a ZeroDivisionError and a negative value
    a notify storm; main() must reject both with a clean argparse error
    (exit 2) before any port is opened."""

    def run_cli(self, *extra):
        script = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                              "..", "..", "host_app", "run_case.py")
        import subprocess
        return subprocess.run(
            [sys.executable, script, "COM99"] + list(extra),
            capture_output=True, text=True, timeout=60)

    def test_zero_interval_clean_error(self):
        r = self.run_cli("--interval", "0")
        self.assertEqual(r.returncode, 2)
        self.assertIn("--interval", r.stderr)
        self.assertNotIn("Traceback", r.stderr)

    def test_nonpositive_secs_clean_error(self):
        r = self.run_cli("--secs", "-5")
        self.assertEqual(r.returncode, 2)
        self.assertIn("--secs", r.stderr)
        self.assertNotIn("Traceback", r.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=2)
