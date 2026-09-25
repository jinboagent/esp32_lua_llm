"""Host unit tests for llm_loop.py (H5.1) after the 2026-08-29
convergence onto the stage-5 conventions: unit-rule config resolution
with provider pairs, the 401/404 self-heals, cmd-field response
matching, and the mid-upload -612 watch.

Run:  python tests/host/test_llm_loop.py
"""
import collections
import json
import os
import sys
import unittest
import unittest.mock

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", ".."))
import llm_loop  # noqa: E402


class ConfigTests(unittest.TestCase):
    FILE = {"DASHSCOPE_BASE_URL": "https://dash/v1",
            "DASHSCOPE_API_KEY": "sk-dash",
            "TOKEN_PLAN_BASE_URL": "https://tp/v1",
            "TOKEN_PLAN_API_KEY": "sk-tp",
            "QWEN_MODEL": "qwen3.8-flash"}

    def resolve(self, env, file_env, force_file=False):
        with unittest.mock.patch.dict(os.environ, env, clear=True), \
             unittest.mock.patch.object(llm_loop, "load_env_file",
                                        return_value=file_env):
            return llm_loop.resolve_llm_config(force_file=force_file)

    def test_provider_pairs_dashscope_first(self):
        cfg = self.resolve({}, dict(self.FILE))
        self.assertEqual((cfg["base"], cfg["key"], cfg["model"]),
                         ("https://dash/v1", "sk-dash", "qwen3.8-flash"))

    def test_legacy_triple_wins_over_pairs(self):
        cfg = self.resolve({}, {"LLM_BASE_URL": "https://legacy/v1",
                                "LLM_API_KEY": "sk-legacy",
                                **self.FILE})
        self.assertEqual((cfg["base"], cfg["key"]),
                         ("https://legacy/v1", "sk-legacy"))

    def test_env_unit_wins(self):
        cfg = self.resolve({"LLM_API_KEY": "sk-env"},
                           {"LLM_BASE_URL": "https://file/v1",
                            "LLM_API_KEY": "sk-file"})
        self.assertEqual(cfg["key"], "sk-env")
        self.assertNotEqual(cfg["base"], "https://file/v1")

    def test_nothing_configured(self):
        self.assertIsNone(self.resolve({}, {}))


class HealTests(unittest.TestCase):
    def generate(self, cfg_first, side_effects, file_cfg):
        """Run llm_generate with a pinned first config, mocked requests,
        and a mocked file config for the 401 fallback."""
        with unittest.mock.patch.dict(os.environ, {}, clear=True), \
             unittest.mock.patch.object(llm_loop, "load_env_file",
                                        return_value=file_cfg), \
             unittest.mock.patch.object(
                     llm_loop, "_generate_once",
                     side_effect=side_effects) as gen:
            # llm_generate resolves config itself; pin both resolutions
            # (initial + the 401 fallback's force_file call)
            with unittest.mock.patch.object(
                    llm_loop, "resolve_llm_config",
                    side_effect=[cfg_first, file_cfg]):
                result = llm_loop.llm_generate(
                    [{"addr": "AA:BB:CC:DD:EE:FF", "rssi": -60}],
                    "test goal")
        return result, gen

    def test_401_falls_back_to_file(self):
        good = "function on_adv() return true end"
        result, gen = self.generate(
            {"base": "https://x/v1", "key": "sk-env", "model": "m"},
            [RuntimeError("LLM HTTP 401: bad"), good],
            {"base": "https://dash/v1", "key": "sk-dash", "model": "q"})
        self.assertEqual(gen.call_count, 2)
        self.assertEqual(gen.call_args_list[1].args[0]["key"], "sk-dash")
        self.assertIn("on_adv", result)

    def test_404_heals_api_v1_base(self):
        good = "-- script"
        cfg = {"base": "https://ws-x.maas.aliyuncs.com/api/v1",
               "key": "k", "model": "m"}
        result, gen = self.generate(
            cfg, [RuntimeError("LLM HTTP 404: "), good], cfg)
        self.assertEqual(gen.call_count, 2)
        self.assertEqual(gen.call_args_list[1].args[0]["base"],
                         "https://ws-x.maas.aliyuncs.com"
                         "/compatible-mode/v1")

    def test_500_exits_with_error(self):
        with self.assertRaises(SystemExit):
            self.generate(
                {"base": "https://x/v1", "key": "k", "model": "m"},
                [RuntimeError("LLM HTTP 500: bad")],
                {"base": "https://dash/v1", "key": "sk-dash",
                 "model": "q"})


class UploadTests(unittest.TestCase):
    """The bridge rejects a violating data line IMMEDIATELY (-612) and
    resets the session; upload_script must surface that, not the -611
    that SCRIPT END would return against the dead session."""

    class FakeSerial:
        def __init__(self, responses):
            self.responses = list(responses)
            self.written = []
            self.timeout = 1

        def write(self, b):
            self.written.append(b)

        def flush(self):
            pass

        def readline(self):
            return self.responses.pop(0) if self.responses else b""

        def reset_input_buffer(self):
            pass

    LOAD_OK = b'{"status":"ok","cmd":"script_load","msg":"ready"}\n'
    END_OK = b'{"status":"ok","cmd":"script_end"}\n'
    REJECT = (b'{"status":"error","cmd":"script_data","code":-612,'
              b'"msg":"sandbox violation: \'os.\' is not allowed"}\n')

    def test_clean_upload(self):
        s = self.FakeSerial([self.LOAD_OK, b"", self.END_OK])
        ok, detail = llm_loop.upload_script(s, "return 1")
        self.assertTrue(ok)
        self.assertIn(b"SCRIPT END\n", s.written)

    def test_forbidden_line_surfaces_minus_612(self):
        s = self.FakeSerial([self.LOAD_OK, self.REJECT])
        ok, detail = llm_loop.upload_script(
            s, "local t = os.time()\nreturn t")
        self.assertFalse(ok)
        self.assertIn("-612", detail)
        self.assertIn("sandbox violation", detail)
        self.assertNotIn(b"SCRIPT END\n", s.written)


class ExpectedCmdTests(unittest.TestCase):
    def test_mapping(self):
        self.assertEqual(llm_loop.expected_cmd("CONN TARGET x"),
                         "conn_target")
        self.assertEqual(llm_loop.expected_cmd("SCAN START"), "scan_start")
        self.assertEqual(llm_loop.expected_cmd("STATUS"), "status")
        self.assertIsNone(llm_loop.expected_cmd("LUA EXEC x"))


if __name__ == "__main__":
    unittest.main(verbosity=2)
