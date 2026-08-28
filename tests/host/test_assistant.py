"""Host unit tests for host_app/assistant.py (H5.3).

Run:  python tests/host/test_assistant.py
Covers the pure logic: typed-envelope parsing (strict + fenced fallback),
device-buffer ingress validation and per-plane caps, history token-budget
trimming, context assembly, and the -451 feature-off classifier.
The interactive Prompt loop and live LLM round trips are verified by the
scripted sessions in the process report (harness/02-knowledge/).
"""
import json
import os
import sys
import unittest
import unittest.mock
from collections import deque

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "..", "host_app"))
import assistant  # noqa: E402


class ParseEnvelopeTests(unittest.TestCase):
    def test_four_types_strict(self):
        for t in ("answer", "clarify", "error"):
            env, fenced = assistant.parse_envelope(
                '{"type":"%s","text":"hi"}' % t)
            self.assertEqual(env["type"], t)
            self.assertEqual(env["text"], "hi")
            self.assertFalse(fenced)
        env, _ = assistant.parse_envelope(
            '{"type":"lua","text":"why","code":"return 1"}')
        self.assertEqual(env["type"], "lua")
        self.assertEqual(env["code"], "return 1")

    def test_escaped_lua_code_roundtrip(self):
        code = 'local s = "a\\"b"\nprint(s .. "\\\\")\n'
        raw = json.dumps({"type": "lua", "text": "t", "code": code})
        env, _ = assistant.parse_envelope(raw)
        self.assertEqual(env["code"], code)

    def test_json_fenced_envelope(self):
        env, fenced = assistant.parse_envelope(
            '```json\n{"type":"answer","text":"ok"}\n```')
        self.assertEqual(env["type"], "answer")
        self.assertFalse(fenced)

    def test_invalid_rejected(self):
        for raw in ('{"type":"answer"}',                    # no text
                    '{"type":"nope","text":"x"}',           # unknown type
                    '{"type":"lua","text":"x"}',            # lua w/o code
                    'not json at all',
                    '{"type":"answer","text":5}'):          # text not str
            env, fenced = assistant.parse_envelope(raw)
            self.assertIsNone(env, raw)
            self.assertFalse(fenced, raw)

    def test_fenced_lua_fallback(self):
        raw = ('Sure, here is the script:\n```lua\nfunction on_adv()\n'
               '  return true\nend\n```\nanything after')
        env, fenced = assistant.parse_envelope(raw)
        self.assertEqual(env["type"], "lua")
        self.assertIn("function on_adv", env["code"])
        self.assertTrue(fenced)

    def test_fenced_extraction_gated_off(self):
        """The fence is the LAST resort: with allow_fenced=False (the
        first parse attempt) the same reply must NOT be extracted - the
        caller owes the model one escaping-focused retry first."""
        raw = ('Sure, here is the script:\n```lua\nfunction on_adv()\n'
               '  return true\nend\n```\nanything after')
        env, fenced = assistant.parse_envelope(raw, allow_fenced=False)
        self.assertIsNone(env)
        self.assertFalse(fenced)

    def test_no_fallback_when_no_lua_block(self):
        env, fenced = assistant.parse_envelope("plain prose, no code")
        self.assertIsNone(env)
        self.assertFalse(fenced)


class DeviceBufferTests(unittest.TestCase):
    def test_classify_lines(self):
        b = assistant.DeviceBuffer()
        self.assertEqual(b.add_line(
            '{"addr":"AA:BB:CC:DD:EE:FF","rssi":-70,"ts":1}'), "adv")
        self.assertEqual(b.add_line(
            '{"ts":9,"addr":"AA:BB:CC:DD:EE:FF","src":"conn","v":1}'),
            "conn")
        self.assertEqual(b.add_line('{"status":"ok","cmd":"scan_start"}'),
                         "resp")
        self.assertIsNone(b.add_line("CONN: garbage line"))
        self.assertIsNone(b.add_line("{not json"))
        self.assertEqual(len(b.adv), 1)
        self.assertEqual(len(b.conn), 1)
        self.assertEqual(b.dropped, 2)
        self.assertEqual(b.console[-1], "{not json")

    def test_conn_status_response_is_not_adv(self):
        """CONN STATUS echoes the peer address; a response must never
        enter the adv plane (addr is tested after status)."""
        b = assistant.DeviceBuffer()
        r = b.add_line('{"status":"ok","cmd":"conn_status",'
                       '"state":"active","addr":"AA:BB:CC:DD:EE:FF",'
                       '"mode":"notify","mtu":23}')
        self.assertEqual(r, "resp")
        self.assertEqual(len(b.adv), 0)

    def test_per_plane_caps(self):
        b = assistant.DeviceBuffer()
        self.assertEqual(b.adv.maxlen, assistant.ADV_BUF_MAX)
        self.assertEqual(b.conn.maxlen, assistant.CONN_BUF_MAX)
        for i in range(b.adv.maxlen + 10):
            b.add_line('{"addr":"AA:AA:AA:AA:AA:%02X","rssi":-60}' % (i % 256))
        self.assertEqual(len(b.adv), b.adv.maxlen)   # oldest rolled out
        for i in range(b.conn.maxlen + 10):
            b.add_line('{"src":"conn","addr":"AA:BB:CC:DD:EE:FF","v":%d}'
                       % i)
        self.assertEqual(len(b.conn), b.conn.maxlen)
        self.assertEqual(b.conn[-1]["v"], b.conn.maxlen + 9)  # newest kept

    def test_snapshot_dedups_and_labels(self):
        b = assistant.DeviceBuffer()
        for rssi in (-50, -60, -70):
            b.add_line('{"addr":"AA:BB:CC:DD:EE:FF","rssi":%d}' % rssi)
        b.add_line('{"addr":"11:22:33:44:55:66","rssi":-80}')
        b.add_line('{"src":"conn","addr":"AA:BB:CC:DD:EE:FF","temp":21}')
        snap = b.snapshot()
        self.assertIn("conn lines (analysis only)", snap)
        self.assertIn('"temp":21', snap)
        self.assertEqual(snap.count('"addr":"AA:BB:CC:DD:EE:FF"'),
                         2)  # one adv (newest rssi) + one conn
        self.assertIn('"rssi":-70', snap)  # newest adv per address
        self.assertNotIn('"rssi":-50', snap)

    def test_snapshot_empty(self):
        self.assertEqual(assistant.DeviceBuffer().snapshot(), "")


class HistoryTests(unittest.TestCase):
    def test_trim_oldest_under_budget(self):
        h = assistant.History(char_budget=100)
        h.add("user", "u" * 60)
        h.add("assistant", "a" * 60)
        h.add("user", "u2" * 10)
        h.add("assistant", "a2" * 10)
        view = h.view()
        total = sum(len(t["content"]) for t in view)
        self.assertLessEqual(total, 100)
        self.assertEqual(len(view), 2)          # oldest pair dropped
        self.assertIn("u2", view[0]["content"])

    def test_keeps_everything_under_budget(self):
        h = assistant.History(char_budget=1000)
        h.add("user", "hello")
        h.add("assistant", '{"type":"answer","text":"hi"}')
        self.assertEqual(len(h.view()), 2)


class AssemblyTests(unittest.TestCase):
    def test_build_messages_shape(self):
        h = assistant.History()
        h.add("user", "previous question")
        h.add("assistant", '{"type":"answer","text":"previous answer"}')
        msgs = assistant.build_messages(h.view(), "adv lines: ...",
                                        "which are Apple devices?")
        self.assertEqual(msgs[0]["role"], "system")
        self.assertIn('"src":"conn"', msgs[0]["content"])   # mode boundary
        self.assertIn('"clarify"', msgs[0]["content"])      # envelope types
        self.assertEqual(msgs[1]["content"], "previous question")
        self.assertEqual(msgs[-1]["role"], "user")
        self.assertIn("which are Apple devices?", msgs[-1]["content"])
        self.assertIn("Device data snapshot", msgs[-1]["content"])

    def test_build_messages_without_snapshot(self):
        msgs = assistant.build_messages([], "", "hello")
        self.assertEqual(len(msgs), 2)
        self.assertEqual(msgs[-1]["content"], "hello")


class LlmConfigTests(unittest.TestCase):
    """Env vars win as a UNIT; a foreign env key must never mix with the
    file's base URL (2026-08-28 401 incident). The file may carry the
    legacy LLM_* triple OR provider pairs (same rule as run_case.py)."""

    FILE = {"LLM_BASE_URL": "https://file.example/v1",
            "LLM_API_KEY": "sk-file",
            "LLM_MODEL": "file-model"}

    def resolve(self, env, file_env):
        with unittest.mock.patch.dict(os.environ, env, clear=True), \
             unittest.mock.patch.object(assistant, "load_env_file",
                                        return_value=dict(file_env)):
            return assistant.resolve_llm_config()

    def test_file_used_when_env_silent(self):
        cfg = self.resolve({}, self.FILE)
        self.assertEqual(cfg["base"], "https://file.example/v1")
        self.assertEqual(cfg["key"], "sk-file")
        self.assertEqual(cfg["model"], "file-model")

    def test_env_wins_as_a_unit(self):
        cfg = self.resolve({"LLM_API_KEY": "sk-env"}, self.FILE)
        self.assertEqual(cfg["key"], "sk-env")
        self.assertEqual(cfg["base"], "https://api.openai.com/v1")  # default
        self.assertNotEqual(cfg["base"], self.FILE["LLM_BASE_URL"])

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

    def test_incomplete_pair_ignored(self):
        # a pair only counts when BOTH its base URL and key are present
        cfg = self.resolve({}, {"DASHSCOPE_BASE_URL": "https://dash/v1",
                                "TOKEN_PLAN_BASE_URL": "https://tp/v1",
                                "TOKEN_PLAN_API_KEY": "sk-tp"})
        self.assertEqual((cfg["base"], cfg["key"]),
                         ("https://tp/v1", "sk-tp"))

    def test_no_key_anywhere(self):
        self.assertIsNone(self.resolve({}, {}))


class AskLlmRetryOrderTests(unittest.TestCase):
    """Rec1 order: ONE escaping-focused retry first; fenced-lua
    extraction only on the (last-resort) second parse."""

    def setUp(self):
        self.sess = assistant.Session.__new__(assistant.Session)
        self.sess.tee = unittest.mock.Mock()
        self.sess.buf = assistant.DeviceBuffer()
        self.sess.history = assistant.History()
        self.sess.cfg = {"base": "https://x/v1", "key": "k", "model": "m"}
        self.sess.system_prompt = assistant.SYSTEM_PROMPT
        self.sess.reader = unittest.mock.Mock()
        self.sess.reader.poll_line.return_value = ""    # input closed
        self.sess.s = None       # no device: deploy confirm ends at EOF

    def test_fenced_first_reply_still_gets_the_retry(self):
        fenced = ('here you go:\n```lua\nfunction on_adv() return true '
                  'end\n```')
        with unittest.mock.patch.object(assistant, "llm_chat",
                                        side_effect=[fenced, fenced]) \
                as chat:
            self.sess.ask_llm("filter apple adverts")
        self.assertEqual(chat.call_count, 2)          # the retry happened
        retry_prompt = chat.call_args_list[1].args[1][-1]["content"]
        self.assertIn("not a valid JSON envelope", retry_prompt)
        self.sess.tee.say.assert_any_call(
            "(recovered the artifact from a fenced lua block after two "
            "invalid envelopes)")

    def test_valid_first_reply_no_retry(self):
        with unittest.mock.patch.object(
                assistant, "llm_chat",
                return_value='{"type":"answer","text":"hi"}') as chat:
            self.sess.ask_llm("hello")
        self.assertEqual(chat.call_count, 1)


class UploadScriptTests(unittest.TestCase):
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
        # the b"" models the read timeout after a (silently acked) line
        s = self.FakeSerial([self.LOAD_OK, b"", self.END_OK])
        ok, detail = assistant.upload_script(s, "return 1", None)
        self.assertTrue(ok)
        self.assertEqual(detail, "uploaded")
        self.assertIn(b"SCRIPT END\n", s.written)

    def test_forbidden_line_surfaces_minus_612(self):
        s = self.FakeSerial([self.LOAD_OK, self.REJECT])
        ok, detail = assistant.upload_script(
            s, "local t = os.time()\nreturn t", None)
        self.assertFalse(ok)
        self.assertIn("-612", detail)
        self.assertIn("sandbox violation", detail)
        # the upload aborted at the offending line: no SCRIPT END attempt
        self.assertNotIn(b"SCRIPT END\n", s.written)


class SystemPromptTests(unittest.TestCase):
    """--system-file replaces the built-in prompt; --system-extra
    appends to it; build_messages honors the override."""

    def write(self, text):
        import tempfile
        with tempfile.NamedTemporaryFile("w", suffix=".txt",
                                         delete=False,
                                         encoding="utf-8") as f:
            f.write(text)
            self.addCleanup(os.unlink, f.name)
            return f.name

    def test_default_is_builtin(self):
        prompt, note = assistant.compose_system_prompt()
        self.assertEqual(prompt, assistant.SYSTEM_PROMPT)
        self.assertEqual(note, "built-in")

    def test_replace(self):
        path = self.write("You are a pirate. Reply in the envelope.")
        prompt, note = assistant.compose_system_prompt(replace_path=path)
        self.assertEqual(prompt, "You are a pirate. Reply in the envelope.")
        self.assertIn("replaced by", note)

    def test_extra_appended(self):
        path = self.write("Always prefix replies with [PILOT].")
        prompt, note = assistant.compose_system_prompt(extra_path=path)
        self.assertEqual(prompt,
                         assistant.SYSTEM_PROMPT
                         + "\n\nAlways prefix replies with [PILOT].")
        self.assertIn("extra", note)

    def test_replace_then_extra(self):
        r = self.write("base rules")
        e = self.write("extra rules")
        prompt, note = assistant.compose_system_prompt(r, e)
        self.assertEqual(prompt, "base rules\n\nextra rules")
        self.assertIn("replaced by", note)
        self.assertIn("extra", note)

    def test_build_messages_uses_override(self):
        msgs = assistant.build_messages([], "", "hi",
                                        system_prompt="CUSTOM")
        self.assertEqual(msgs[0]["content"], "CUSTOM")


class Fallback401Tests(unittest.TestCase):
    """An ambient shell key can hijack the config (env-wins-as-unit) and
    401 against the wrong provider - observed twice on this machine.
    On 401 with an env-sourced config, switch once to .llm_env."""

    FILE = {"DASHSCOPE_BASE_URL": "https://dash/v1",
            "DASHSCOPE_API_KEY": "sk-file",
            "QWEN_MODEL": "qwen-flash"}
    ENV = {"LLM_API_KEY": "sk-env-expired"}

    def setUp(self):
        self.sess = assistant.Session.__new__(assistant.Session)
        self.sess.tee = unittest.mock.Mock()
        self.sess.buf = assistant.DeviceBuffer()
        self.sess.history = assistant.History()
        self.sess.reader = unittest.mock.Mock()
        self.sess.reader.poll_line.return_value = ""
        self.sess.s = None
        self.sess.system_prompt = assistant.SYSTEM_PROMPT

    def test_switch_on_401(self):
        with unittest.mock.patch.dict(os.environ, self.ENV, clear=True), \
             unittest.mock.patch.object(assistant, "load_env_file",
                                        return_value=dict(self.FILE)):
            self.sess.cfg = assistant.resolve_llm_config()
            self.sess.cfg_from_env = True
            ok = self.sess._try_401_fallback(
                RuntimeError("LLM HTTP 401: invalid_api_key"))
        self.assertTrue(ok)
        self.assertEqual(self.sess.cfg["key"], "sk-file")
        self.assertEqual(self.sess.cfg["base"], "https://dash/v1")
        self.assertFalse(self.sess.cfg_from_env)
        self.assertIn("switching to .llm_env",
                      str(self.sess.tee.say.call_args))

    def test_no_fallback_when_config_from_file(self):
        self.sess.cfg = {"base": "b", "key": "k", "model": "m"}
        self.sess.cfg_from_env = False
        self.assertFalse(self.sess._try_401_fallback(
            RuntimeError("LLM HTTP 401: x")))

    def test_no_fallback_on_other_errors(self):
        self.sess.cfg = {"base": "b", "key": "k", "model": "m"}
        self.sess.cfg_from_env = True
        self.assertFalse(self.sess._try_401_fallback(
            RuntimeError("LLM HTTP 500: x")))

    def test_no_fallback_when_same_key(self):
        self.sess.cfg = {"base": "b", "key": "sk-env-expired",
                         "model": "m"}
        self.sess.cfg_from_env = True
        with unittest.mock.patch.dict(os.environ, self.ENV, clear=True), \
             unittest.mock.patch.object(
                 assistant, "load_env_file",
                 return_value={"LLM_API_KEY": "sk-env-expired"}):
            self.assertFalse(self.sess._try_401_fallback(
                RuntimeError("LLM HTTP 401: x")))

    def test_force_file_ignores_env(self):
        with unittest.mock.patch.dict(os.environ, self.ENV, clear=True), \
             unittest.mock.patch.object(assistant, "load_env_file",
                                        return_value=dict(self.FILE)):
            cfg = assistant.resolve_llm_config(force_file=True)
        self.assertEqual(cfg["key"], "sk-file")

    def test_ask_llm_recovers_after_401(self):
        good = '{"type":"answer","text":"ok"}'
        with unittest.mock.patch.dict(os.environ, self.ENV, clear=True), \
             unittest.mock.patch.object(assistant, "load_env_file",
                                        return_value=dict(self.FILE)):
            self.sess.cfg = {"base": "https://x/v1", "key": "sk-env",
                             "model": "m"}
            self.sess.cfg_from_env = True
            with unittest.mock.patch.object(
                    assistant, "llm_chat",
                    side_effect=[RuntimeError("LLM HTTP 401: bad"),
                                 good]) as chat:
                self.sess.ask_llm("hello")
        self.assertEqual(chat.call_count, 2)
        self.assertEqual(chat.call_args_list[1].args[0]["key"], "sk-file")


class BasePathHealTests(unittest.TestCase):
    """A base ending /api/v1 is the native DashScope root; chat must
    retry once on the same host's /compatible-mode/v1 (announced)."""

    def test_heal_on_404(self):
        cfg = {"base": "https://ws-x.maas.aliyuncs.com/api/v1",
               "key": "k", "model": "m"}
        notes = []
        with unittest.mock.patch.object(
                assistant, "_chat_once",
                side_effect=[RuntimeError("LLM HTTP 404: "),
                             "fine"]) as once:
            out = assistant.llm_chat(cfg, [], note=notes.append)
        self.assertEqual(out, "fine")
        self.assertEqual(once.call_count, 2)
        self.assertEqual(cfg["base"],
                         "https://ws-x.maas.aliyuncs.com"
                         "/compatible-mode/v1")   # healed, session-sticky
        self.assertTrue(any("404" in n and "compatible-mode" in n
                            for n in notes))

    def test_no_heal_without_api_v1_base(self):
        cfg = {"base": "https://api.example.com/v1", "key": "k",
               "model": "m"}
        with unittest.mock.patch.object(
                assistant, "_chat_once",
                side_effect=RuntimeError("LLM HTTP 404: body")):
            with self.assertRaises(RuntimeError):
                assistant.llm_chat(cfg, [])
        self.assertEqual(cfg["base"], "https://api.example.com/v1")

    def test_no_heal_on_500(self):
        cfg = {"base": "https://ws-x.maas.aliyuncs.com/api/v1",
               "key": "k", "model": "m"}
        with unittest.mock.patch.object(
                assistant, "_chat_once",
                side_effect=RuntimeError("LLM HTTP 500: oops")):
            with self.assertRaises(RuntimeError):
                assistant.llm_chat(cfg, [])


class FeatureOffTests(unittest.TestCase):
    """R2: a conn-feature-off build answers CONN commands with -451."""

    def test_451_detected(self):
        r = {"status": "error", "cmd": "conn", "code": -451,
             "msg": "connection feature not compiled in"}
        self.assertEqual(assistant.describe_resp(r),
                         "conn not enabled on this firmware")

    def test_other_errors_verbatim(self):
        r = {"status": "error", "cmd": "conn_start", "code": -455}
        self.assertIn("-455", assistant.describe_resp(r))
        self.assertEqual(assistant.describe_resp(None),
                         "no response (timeout)")


if __name__ == "__main__":
    unittest.main(verbosity=2)
