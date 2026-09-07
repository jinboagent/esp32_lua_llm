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
import collections
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
        self.sess.tools = assistant.ToolRegistry()
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
        self.sess.tools = assistant.ToolRegistry()

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


class GoldenTranscriptTests(unittest.TestCase):
    """Real-world line shapes from the 2026-08-28 sessions, frozen as a
    fixture: adv/conn JSON, CLI echoes, boot logs, ANSI-wrapped NimBLE
    logs, garbage, and the historical malformed TARGET response. The
    buffer must classify every one correctly and never crash."""

    FIXTURE = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                           "fixtures", "device_stream_sample.txt")

    def setUp(self):
        with open(self.FIXTURE, encoding="utf-8") as f:
            self.lines = [l.rstrip("\n") for l in f if l.strip()]

    def test_classification_counts(self):
        b = assistant.DeviceBuffer()
        kinds = [b.add_line(l) for l in self.lines]
        self.assertEqual(b.adv and sum(k == "adv" for k in kinds), 3)
        self.assertEqual(sum(k == "conn" for k in kinds), 2)
        self.assertEqual(sum(k == "resp" for k in kinds), 3)
        # dropped: echo, 2 boot logs, garbage, malformed target, 2 ANSI
        self.assertEqual(b.dropped, 7)

    def test_malformed_target_never_enters_a_plane(self):
        b = assistant.DeviceBuffer()
        for l in self.lines:
            b.add_line(l)
        addrs = {a.get("addr") for a in b.adv}
        self.assertNotIn("12345678-1234-1234-1234-123456789abc", addrs)
        self.assertFalse(any("conn_target" in json.dumps(c)
                             for c in b.conn))

    def test_snapshot_from_real_stream(self):
        b = assistant.DeviceBuffer()
        for l in self.lines:
            b.add_line(l)
        snap = b.snapshot()
        self.assertEqual(b.conn[0]["src"], "conn")     # classified
        self.assertIn("conn lines (analysis only)", snap)
        self.assertIn("7 invalid dropped", snap)

    def test_cmd_json_over_device_sim(self):
        """The reader used by every command exchange against the same
        fixture stream: returns the status line for the command issued,
        tolerating all the noise around it."""
        import collections
        q = collections.deque(l.encode() for l in self.lines)

        class Sim:
            timeout = 1

            def reset_input_buffer(self):
                pass

            def write(self, b):
                pass

            def flush(self):
                pass

            def readline(self):
                return q.popleft() if q else b""

        r = assistant.cmd_json(Sim(), "CONN STOP")
        self.assertEqual(r.get("cmd"), "conn_stop")
        self.assertEqual(r.get("code"), -453)


class DeviceSimSerial:
    """Models the dongle's console for full-REPL tests: a map from
    command text -> response lines (echo + JSON); reset_input_buffer
    drops pending output like the real port; readline pops or times
    out empty. Records every line written for cleanup assertions."""

    def __init__(self, responses):
        self.responses = responses
        self.pending = collections.deque()
        self.written = []
        self.timeout = 1
        self.closed = False

    def write(self, b):
        text = b.decode().strip()
        self.written.append(text)
        self.pending.extend(self.responses.get(text, []))

    def flush(self):
        pass

    def reset_input_buffer(self):
        self.pending.clear()

    def readline(self):
        return self.pending.popleft() if self.pending else b""

    def close(self):
        self.closed = True


class ReplLoopTests(unittest.TestCase):
    """The message-Prompt loop itself (previously only its pure
    fragments were unit-tested — the 2026-08-28 tick-units bug lived
    exactly in that gap, on the H5.2 side). Runs whole sessions against
    a DeviceSimSerial with piped stdin; the tee log is read back for
    assertions."""

    STATUS_IDLE = ('{"status":"ok","cmd":"status","state":"idle",'
                   '"scanning":false,"v":1}\n').encode()
    SCAN_START = ('{"status":"ok","cmd":"scan_start"}\n').encode()
    SCRIPT_STOP = ('{"status":"error","cmd":"script_stop","code":-911,'
                   '"msg":"invalid state: no script running"}\n').encode()
    SCAN_STOP = ('{"status":"ok","cmd":"scan_stop"}\n').encode()
    CONN_STOP = ('{"status":"error","cmd":"conn_stop","code":-453}\n'
                 ).encode()

    def responses(self):
        return {
            "STATUS": [self.STATUS_IDLE],
            "SCAN START": [self.SCAN_START],
            "SCRIPT STOP": [self.SCRIPT_STOP],
            "SCAN STOP": [self.SCAN_STOP],
            "CONN STOP": [self.CONN_STOP],
        }

    def run_session(self, user_lines, sim, no_llm=True, cfg=None,
                    llm_replies=None):
        import io
        import tempfile
        cwd = os.getcwd()
        tmp = tempfile.mkdtemp()
        self.addCleanup(os.chdir, cwd)
        os.chdir(tmp)                       # tee logs land in tmp
        stdin = io.StringIO("".join(l + "\n" for l in user_lines))
        with unittest.mock.patch("sys.stdin", new=stdin), \
             unittest.mock.patch.object(assistant.serial, "Serial",
                                        return_value=sim):
            sess = assistant.Session("COMTEST", no_llm=no_llm)
            if cfg is not None:
                sess.cfg = cfg
                sess.cfg_from_env = False
            if llm_replies is not None:
                it = iter(llm_replies)
                unittest.mock.patch.object(
                    assistant, "llm_chat",
                    side_effect=lambda *a, **k: next(it)).start()
                self.addCleanup(unittest.mock.patch.stopall)
            rc = sess.run()
        logs = [f for f in os.listdir(tmp) if f.endswith(".log")]
        self.assertEqual(len(logs), 1, "exactly one tee transcript")
        with open(os.path.join(tmp, logs[0]), encoding="utf-8") as f:
            return rc, f.read()

    def test_full_no_llm_session(self):
        sim = DeviceSimSerial(self.responses())
        rc, log = self.run_session(
            ["/help", "/scan on", "hello there", "/quit"], sim)
        self.assertEqual(rc, 0)
        self.assertIn("you: /help", log)
        self.assertIn("commands:", log)
        self.assertIn("(scan started", log)
        self.assertIn("--no-llm mode - the prompt was NOT sent", log)
        self.assertIn("transcript saved:", log)
        self.assertIn("(exit - cleaning up)", log)
        # cleanup order on the wire: SCRIPT STOP, SCAN STOP, CONN STOP
        tail = [w for w in sim.written if w in
                ("SCRIPT STOP", "SCAN STOP", "CONN STOP")][-3:]
        self.assertEqual(tail,
                         ["SCRIPT STOP", "SCAN STOP", "CONN STOP"])
        self.assertTrue(sim.closed)          # N3: close only at exit

    def test_keyboard_interrupt_cleans_up(self):
        sim = DeviceSimSerial(self.responses())
        with unittest.mock.patch.object(
                assistant.Session, "handle_line",
                side_effect=KeyboardInterrupt):
            rc, log = self.run_session(["whatever"], sim)
        self.assertEqual(rc, 0)
        self.assertIn("(interrupted - cleaning up)", log)
        self.assertEqual(
            [w for w in sim.written if w.endswith("STOP")],
            ["SCRIPT STOP", "SCAN STOP", "CONN STOP"])
        self.assertTrue(sim.closed)

    def test_free_text_routes_envelope_in_loop(self):
        sim = DeviceSimSerial(self.responses())
        rc, log = self.run_session(
            ["summarize", "/quit"], sim, no_llm=False,
            cfg={"base": "https://x/v1", "key": "k", "model": "stub"},
            llm_replies=['{"type":"answer","text":"all good"}'])
        self.assertEqual(rc, 0)
        self.assertIn("(asking stub", log)
        self.assertIn("llm: all good", log)
        # the turn entered history (visible via /history transcript not
        # requested here; assert via a second turn seeing the first)
        self.assertIn("you: summarize", log)


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


# ---- H6.1 tool registry ------------------------------------------------------

DEMO_MANIFEST = (
    '{"version":1,"name":"demo","tools":['
    '{"name":"mean","doc":"Arithmetic mean of a table of numbers.",'
    '"args":[{"name":"numbers","type":"table","item_type":"number"}],'
    '"returns":"string","mutating":false,'
    '"example":{"args":{"numbers":[3,5,10]},"result":"6.00"}},'
    '{"name":"temp_convert","doc":"Temperature conversion.",'
    '"args":[{"name":"value","type":"number"},'
    '{"name":"unit","type":"string","enum":["c","f"]}],'
    '"returns":"string","mutating":false,'
    '"example":{"args":{"value":100,"unit":"c"},"result":"212.0F"}},'
    '{"name":"bench_reset","doc":"Reset the demo counter.",'
    '"args":[],"returns":"ack","mutating":true,'
    '"example":{"args":{},"result":"ok: bench reset"}}]}')


class ScanLuaLineTests(unittest.TestCase):
    """Bridge token parity (lua_llm_bridge.c): dotted tokens match
    anywhere, bare words need identifier boundaries, tokens inside
    string literals are rejected too - fail-closed by design."""

    def test_dotted_tokens_caught(self):
        for line in ("local t = os.time()", "x = io.read()",
                     "debug.traceback()", "return package.path",
                     's = "call os.system now"'):   # inside a string too
            self.assertIsNotNone(assistant.scan_lua_line(line), line)

    def test_words_need_boundaries(self):
        for line in ("return load('x')", "require('x')",
                     "collectgarbage()", "dofile('x')", "loadfile('x')"):
            self.assertIsNotNone(assistant.scan_lua_line(line), line)

    def test_clean_lines_pass(self):
        for line in ("payload = 1",          # 'load' inside a word: ok
                     "download_count = 2",
                     "return string.format('%.2f', 6/3)",
                     "function mean(a) return a[1] end"):
            self.assertIsNone(assistant.scan_lua_line(line), line)


class LuaExecLinesTests(unittest.TestCase):
    def test_comments_and_blanks_skipped(self):
        lines, err = assistant.lua_exec_lines(
            "-- header comment\n\nlocal_ok = 1\n  -- indented comment\n")
        self.assertIsNone(err)
        self.assertEqual(lines, ["local_ok = 1"])

    def test_too_long_line_rejected(self):
        lines, err = assistant.lua_exec_lines('x = "' + "a" * 250 + '"')
        self.assertIsNone(lines)
        self.assertIn("240-byte", err)

    def test_forbidden_line_rejected(self):
        lines, err = assistant.lua_exec_lines("t = os.time()")
        self.assertIsNone(lines)
        self.assertIn("os.", err)

    def test_empty_source_rejected(self):
        lines, err = assistant.lua_exec_lines("-- nothing executable\n")
        self.assertIsNone(lines)
        self.assertIn("no executable lines", err)

    def test_max_lines_enforced(self):
        lines, err = assistant.lua_exec_lines(
            "\n".join(f"x{i} = {i}" for i in range(5)), max_lines=4)
        self.assertIsNone(lines)
        self.assertIn("more than 4", err)

    def test_real_demo_pack_passes(self):
        """The shipped demo pack obeys its own convention: every kept
        line is within the budget and the scan is clean."""
        path = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                            "..", "..", "host_app", "tool_packs", "demo.lua")
        with open(path, encoding="utf-8") as f:
            lines, err = assistant.lua_exec_lines(f.read())
        self.assertIsNone(err)
        self.assertTrue(all(len(l) <= assistant.LUA_EXEC_LINE_MAX
                            for l in lines))
        self.assertTrue(any("function manifest" in l for l in lines))
        self.assertEqual(len([l for l in lines
                              if l.startswith("function ")]), 5)

    def test_demo_pack_manifest_assembles_valid(self):
        """Replay the pack's DEMO_M concatenation lines host-side and
        validate the assembled manifest - pins the JSON assembly (the
        first live session caught a stray closing brace here: the device
        rejected the pack at per-line compile, 2026-09-05)."""
        import re
        path = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                            "..", "..", "host_app", "tool_packs", "demo.lua")
        with open(path, encoding="utf-8") as f:
            lines, _ = assistant.lua_exec_lines(f.read())
        m = ""
        for ln in lines:
            first = re.match(r"^DEMO_M = \[\[(.*)\]\]$", ln)
            more = re.match(r"^DEMO_M = DEMO_M \.\. \[\[(.*)\]\]$", ln)
            if first:
                m = first.group(1)
            elif more:
                m += more.group(1)
        manifest, err = assistant.validate_manifest(m)
        self.assertIsNone(err, err)
        self.assertEqual(manifest["name"], "demo")
        self.assertEqual(len(manifest["tools"]), 3)


class ValidateManifestTests(unittest.TestCase):
    def test_demo_manifest_valid(self):
        m, err = assistant.validate_manifest(DEMO_MANIFEST)
        self.assertIsNone(err)
        self.assertEqual(m["name"], "demo")
        self.assertEqual([t["name"] for t in m["tools"]],
                         ["mean", "temp_convert", "bench_reset"])

    def test_rejections(self):
        bad = {
            "not json": "nope{",
            "bad version": '{"version":2,"name":"x",'
                           '"tools":[{"name":"a","doc":"d"}]}',
            "bad name": '{"version":1,"name":"not-ok",'
                        '"tools":[{"name":"a","doc":"d"}]}',
            "empty tools": '{"version":1,"name":"x","tools":[]}',
            "dup names": '{"version":1,"name":"x",'
                         '"tools":[{"name":"a","doc":"d"},'
                         '{"name":"a","doc":"d"}]}',
            "reserved name": '{"version":1,"name":"x",'
                             '"tools":[{"name":"on_adv","doc":"d"}]}',
            "no doc": '{"version":1,"name":"x","tools":[{"name":"a"}]}',
            "bad arg type": '{"version":1,"name":"x","tools":[{"name":"a",'
                            '"doc":"d","args":[{"name":"n","type":"float"}]}]}',
        }
        for label, text in bad.items():
            m, err = assistant.validate_manifest(text)
            self.assertIsNone(m, label)
            self.assertTrue(err, label)


class ToolRegistryTests(unittest.TestCase):
    def _demo(self):
        m, _ = assistant.validate_manifest(DEMO_MANIFEST)
        return m

    def test_add_and_collisions(self):
        reg = assistant.ToolRegistry()
        pack, err = reg.add(self._demo(), "demo.lua")
        self.assertIsNone(err)
        self.assertEqual(len(pack["tools"]), 3)
        _, err = reg.add(self._demo(), "demo.lua")
        self.assertIn("already registered", err)
        other = assistant.validate_manifest(
            DEMO_MANIFEST.replace('"name":"demo"', '"name":"other"'))[0]
        _, err = reg.add(other, "other.lua")   # same tool names
        self.assertIn("collision", err)

    def test_prompt_empty_when_no_packs(self):
        self.assertEqual(assistant.ToolRegistry().prompt(), "")

    def test_prompt_lists_tools_and_rules(self):
        reg = assistant.ToolRegistry()
        reg.add(self._demo(), "host_app/tool_packs/demo.lua")
        p = reg.prompt()
        self.assertIn("TOOL REGISTRY", p)
        self.assertIn("mean(numbers: table)", p)
        self.assertIn('example: mean({numbers=[3, 5, 10]}) -> "6.00"', p)
        self.assertIn("[mutating]", p)
        self.assertIn("240", p)
        self.assertIn("on_adv", p)

    def test_describe_listing(self):
        reg = assistant.ToolRegistry()
        reg.add(self._demo(), "demo.lua")
        d = reg.describe()
        self.assertIn("1 pack(s), 3 tool(s)", d)
        self.assertIn("bench_reset() -> ack  [mutating]", d)


class DeployArtifactTests(unittest.TestCase):
    """The routing rule: hooks/manifest definitions are deploy artifacts
    (human gate); tool compositions are programs (autonomous)."""

    def test_hooks_and_manifest_are_deploy_artifacts(self):
        for code in ("function on_adv(a) return true end",
                     "transform = function(a, j) return j end",
                     "function manifest() return M end",
                     "-- on_adv = function (mentioned in a comment)"):
            self.assertIsNotNone(assistant.DEPLOY_RE.search(code), code)

    def test_tool_programs_are_not(self):
        for code in ("return mean({numbers={3,5,10}})",
                     'return temp_convert({value=100,unit="c"})',
                     "local t = mean({numbers={1}}) return t"):
            self.assertIsNone(assistant.DEPLOY_RE.search(code), code)


class ExpectedCmdLuaTests(unittest.TestCase):
    def test_lua_exec_maps_to_cmd_field(self):
        """Every LUA exchange matches responses by cmd field - the
        2026-08-28 stale-line lesson, extended to the tool path."""
        self.assertEqual(assistant.expected_cmd("LUA EXEC return 1"),
                         "lua_exec")
        self.assertEqual(assistant.expected_cmd("LUA INIT"), "lua_init")
        self.assertEqual(assistant.expected_cmd("SCAN STOP"), "scan_stop")


class FetchManifestTests(unittest.TestCase):
    """Chunked string.sub fetch over a canned serial; also the
    engine-not-initialized self-heal."""

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

    @staticmethod
    def lua_ok(result):
        return ('{"status":"ok","cmd":"lua_exec","result":'
                + json.dumps(result) + "}\n").encode()

    def _sess(self, sim):
        sess = assistant.Session.__new__(assistant.Session)
        sess.tee = unittest.mock.Mock()
        sess.buf = assistant.DeviceBuffer()
        sess.s = sim
        return sess

    def test_chunks_are_joined(self):
        m = DEMO_MANIFEST
        step = assistant.MANIFEST_CHUNK
        chunks = [m[i:i + step] for i in range(0, len(m), step)]
        if len(chunks[-1]) == step:
            chunks.append("")
        sim = self.FakeSerial([self.lua_ok(c) for c in chunks])
        text, err = self._sess(sim).fetch_manifest()
        self.assertIsNone(err)
        self.assertEqual(text, m)
        sent = [w.decode().strip() for w in sim.written]
        self.assertIn("LUA EXEC return string.sub(manifest(),1,180)", sent)
        self.assertIn("LUA EXEC return string.sub(manifest(),181,360)", sent)

    def test_engine_not_initialized_heals_once(self):
        not_ready = (b'{"status":"error","cmd":"lua_exec",'
                     b'"msg":"Lua engine not initialized"}\n')
        init_ok = b'{"status":"ok","cmd":"lua_init"}\n'
        m = '{"version":1,"name":"x","tools":[{"name":"a","doc":"d"}]}'
        sim = self.FakeSerial([not_ready, init_ok, self.lua_ok(m)])
        text, err = self._sess(sim).fetch_manifest()
        self.assertIsNone(err)
        self.assertEqual(text, m)
        self.assertIn("LUA INIT", [w.decode().strip() for w in sim.written])


class ToolReplTests(ReplLoopTests):
    """Whole-session flows on the DeviceSimSerial: /tools load is
    confirm-gated, tool programs execute autonomously with the result fed
    back to the LLM, the execution cap forces a final answer, and hook
    artifacts keep the human deploy gate."""

    PACK = "\n".join([
        "-- test pack",
        'TM = [[{"version":1,"name":"tpack","tools":[{]]',
        'TM = TM .. [["name":"mean","doc":"Arithmetic mean of numbers.",'
        '"args":[{"name":"numbers","type":"table","item_type":"number"}],'
        '"returns":"string","mutating":false,'
        '"example":{"args":{"numbers":[3,5,10]},"result":"6.00"}}]}]]',
        "function manifest() return TM end",
        "function mean(a) local s=0 for i=1,#a do s=s+a[i] end "
        "return string.format(\"%.2f\",s/#a) end",
    ])

    PACK_MANIFEST = (
        '{"version":1,"name":"tpack","tools":[{"name":"mean",'
        '"doc":"Arithmetic mean of numbers.","args":[{"name":"numbers",'
        '"type":"table","item_type":"number"}],"returns":"string",'
        '"mutating":false,"example":{"args":{"numbers":[3,5,10]},'
        '"result":"6.00"}}]}')

    @staticmethod
    def lua_ok(result):
        return ('{"status":"ok","cmd":"lua_exec","result":'
                + json.dumps(result) + "}\n").encode()

    def lua_pack_responses(self):
        r = dict(self.responses())
        for ln in self.PACK.splitlines():
            s = ln.strip()
            if s and not s.startswith("--"):
                r["LUA EXEC " + s] = [self.lua_ok("")]
        m = self.PACK_MANIFEST
        step = assistant.MANIFEST_CHUNK
        chunks = [m[i:i + step] for i in range(0, len(m), step)]
        if len(chunks[-1]) == step:
            chunks.append("")
        for i, c in enumerate(chunks):
            lo = i * step + 1
            r[f"LUA EXEC return string.sub(manifest(),{lo},{lo + step - 1})"] \
                = [self.lua_ok(c)]
        return r

    def write_pack(self):
        import tempfile
        with tempfile.NamedTemporaryFile("w", suffix=".lua",
                                         delete=False,
                                         encoding="utf-8") as f:
            f.write(self.PACK)
            self.addCleanup(os.unlink, f.name)
            return f.name

    def test_tools_load_and_list(self):
        sim = DeviceSimSerial(self.lua_pack_responses())
        pack = self.write_pack()
        rc, log = self.run_session(
            [f"/tools load {pack}", "y", "/tools", "/quit"], sim)
        self.assertEqual(rc, 0)
        self.assertIn("register pack", log)
        self.assertIn("confirm> y", log)
        self.assertIn("pack 'tpack' registered: 1 tools", log)
        self.assertIn("mean(numbers: table) -> string", log)
        self.assertIn('example: mean({numbers=[3, 5, 10]}) -> "6.00"', log)
        self.assertIn("LUA EXEC function manifest() return TM end",
                      sim.written)

    def test_tools_load_declined_sends_nothing(self):
        sim = DeviceSimSerial(self.lua_pack_responses())
        pack = self.write_pack()
        rc, log = self.run_session(
            [f"/tools load {pack}", "n", "/quit"], sim)
        self.assertEqual(rc, 0)
        self.assertIn("(aborted - nothing was sent to the device)", log)
        self.assertFalse([w for w in sim.written
                          if w.startswith("LUA EXEC")])

    def test_tool_program_round_trip(self):
        sim = DeviceSimSerial(self.lua_pack_responses())
        sim.responses["LUA EXEC return mean({numbers={3,5,10}})"] = \
            [self.lua_ok("6.00")]
        pack = self.write_pack()
        program = json.dumps({"type": "lua", "text": "compute",
                              "code": "return mean({numbers={3,5,10}})"})
        rc, log = self.run_session(
            [f"/tools load {pack}", "y",
             "what is the mean of 3, 5 and 10?", "/quit"],
            sim, no_llm=False,
            cfg={"base": "https://x/v1", "key": "k", "model": "stub"},
            llm_replies=[program,
                         '{"type":"answer","text":"the mean is 6.00"}'])
        self.assertEqual(rc, 0)
        self.assertIn("tool> 6.00", log)
        self.assertIn("llm: the mean is 6.00", log)
        self.assertIn("LUA EXEC return mean({numbers={3,5,10}})",
                      sim.written)
        self.assertNotIn("deploy? [y/N]", log)

    def test_hook_artifact_still_deploy_gated(self):
        sim = DeviceSimSerial(self.lua_pack_responses())
        pack = self.write_pack()
        hook = json.dumps({"type": "lua", "text": "filter",
                           "code": "function on_adv(a) return true end"})
        rc, log = self.run_session(
            [f"/tools load {pack}", "y", "keep everything", "/quit"],
            sim, no_llm=False,
            cfg={"base": "https://x/v1", "key": "k", "model": "stub"},
            llm_replies=[hook])
        self.assertEqual(rc, 0)
        self.assertIn("deploy? [y/N]", log)
        self.assertNotIn("tool>", log)
        # the next piped line (/quit) answers the gate: artifact kept,
        # nothing was uploaded to the device
        self.assertIn("(kept as the last artifact - /deploy re-offers it)",
                      log)
        self.assertNotIn("SCRIPT LOAD", sim.written)

    def test_execution_cap_forces_final_answer(self):
        sim = DeviceSimSerial(self.lua_pack_responses())
        sim.responses["LUA EXEC return mean({numbers={3,5,10}})"] = \
            [self.lua_ok("6.00")]
        pack = self.write_pack()
        program = json.dumps({"type": "lua", "text": "again",
                              "code": "return mean({numbers={3,5,10}})"})
        # four programs offered: three execute, the fourth trips the cap
        # and the LLM is told to finalize - which it does
        rc, log = self.run_session(
            [f"/tools load {pack}", "y", "mean please", "/quit"],
            sim, no_llm=False,
            cfg={"base": "https://x/v1", "key": "k", "model": "stub"},
            llm_replies=[program, program, program, program,
                         '{"type":"answer","text":"done"}'])
        self.assertEqual(rc, 0)
        self.assertEqual(log.count("tool> 6.00"), 3)
        self.assertIn("tool execution cap reached (3/turn)", log)
        self.assertIn("llm: done", log)

    def test_device_error_is_corrective_feedback(self):
        sim = DeviceSimSerial(self.lua_pack_responses())
        sim.responses["LUA EXEC return mean({numbers={3,5,10}})"] = [
            ('{"status":"error","cmd":"lua_exec","code":-630,"msg":"attempt '
             'to call a nil value (global \'mean\')"}\n').encode()]
        sim.responses["LUA EXEC return mean({numbers={1,2,3}})"] = \
            [self.lua_ok("2.00")]
        pack = self.write_pack()
        rc, log = self.run_session(
            [f"/tools load {pack}", "y", "mean of 3,5,10", "/quit"],
            sim, no_llm=False,
            cfg={"base": "https://x/v1", "key": "k", "model": "stub"},
            llm_replies=[
                json.dumps({"type": "lua", "text": "try",
                            "code": "return mean({numbers={3,5,10}})"}),
                json.dumps({"type": "lua", "text": "fixed",
                            "code": "return mean({numbers={1,2,3}})"}),
                '{"type":"answer","text":"it is 2.00"}'])
        self.assertEqual(rc, 0)
        self.assertIn("(tool execution failed:", log)
        self.assertIn("tool> 2.00", log)
        self.assertIn("llm: it is 2.00", log)

    COLLIDING_PACK = "\n".join([
        "-- colliding pack (tool name 'mean' collides with tpack)",
        'CM = [[{"version":1,"name":"cpack","tools":[{]]',
        'CM = CM .. [["name":"mean","doc":"colliding mean.",'
        '"args":[{"name":"numbers","type":"table","item_type":"number"}],'
        '"returns":"string","mutating":false,'
        '"example":{"args":{"numbers":[1,2]},"result":"1.50"}}]}]]',
        "function manifest() return CM end",
        'function mean(a) return "1.50" end',
    ])

    def _write_pack_text(self, text):
        import tempfile
        with tempfile.NamedTemporaryFile("w", suffix=".lua", delete=False,
                                         encoding="utf-8") as f:
            f.write(text)
            self.addCleanup(os.unlink, f.name)
            return f.name

    def test_tools_empty_listing(self):
        sim = DeviceSimSerial(self.responses())
        rc, log = self.run_session(["/tools", "/quit"], sim)
        self.assertEqual(rc, 0)
        self.assertIn("(no tool packs registered", log)

    def test_tools_refresh_no_packs(self):
        sim = DeviceSimSerial(self.responses())
        rc, log = self.run_session(["/tools refresh", "/quit"], sim)
        self.assertEqual(rc, 0)
        self.assertIn("(no packs registered)", log)
        self.assertFalse([w for w in sim.written if w.startswith("LUA EXEC")])

    def test_tools_refresh_updates_pack(self):
        sim = DeviceSimSerial(self.lua_pack_responses())
        pack = self.write_pack()
        rc, log = self.run_session(
            [f"/tools load {pack}", "y", "/tools refresh", "/quit"], sim)
        self.assertEqual(rc, 0)
        self.assertIn("pack 'tpack' registered", log)
        self.assertIn("(refreshed pack 'tpack': 1 tools)", log)

    def test_tools_refresh_clears_on_device_reset(self):
        # unit-level: a pack is already registered, then the device Lua
        # state was reset (manifest() gone) - /tools refresh clears it
        class Sim:
            timeout = 1

            def __init__(self, responses):
                self.responses = list(responses)
                self.written = []

            def write(self, b):
                self.written.append(b)

            def flush(self):
                pass

            def readline(self):
                return self.responses.pop(0) if self.responses else b""

            def reset_input_buffer(self):
                pass

        sess = assistant.Session.__new__(assistant.Session)
        sess.tee = unittest.mock.Mock()
        sess.buf = assistant.DeviceBuffer()
        sess.tools = assistant.ToolRegistry()
        m, _ = assistant.validate_manifest(
            '{"version":1,"name":"tpack","tools":[{"name":"mean","doc":"d",'
            '"args":[{"name":"numbers","type":"table","item_type":"number"}],'
            '"returns":"string"}]}')
        sess.tools.add(m, "tpack.lua")
        sess.s = Sim([(
            '{"status":"error","cmd":"lua_exec","code":-630,'
            '"msg":"attempt to call a nil value (global \'manifest\')"}\n'
        ).encode()])
        sess.do_tools_refresh()
        self.assertTrue(sess.tools.is_empty())
        self.assertIn("registry cleared",
                      str(sess.tee.say.call_args_list))

    def test_tools_load_collision_rejected_before_send(self):
        sim = DeviceSimSerial(self.lua_pack_responses())
        pack_a = self.write_pack()
        pack_b = self._write_pack_text(self.COLLIDING_PACK)
        rc, log = self.run_session(
            [f"/tools load {pack_a}", "y",
             f"/tools load {pack_b}", "/quit"], sim)
        self.assertEqual(rc, 0)
        self.assertIn("pack 'tpack' registered", log)
        self.assertIn("name collision with an active pack", log)
        # the colliding pack's lines never reached the device
        self.assertFalse([w for w in sim.written
                          if w.startswith("LUA EXEC CM")])

    def test_tools_load_duplicate_rejected_before_send(self):
        sim = DeviceSimSerial(self.lua_pack_responses())
        pack = self.write_pack()
        rc, log = self.run_session(
            [f"/tools load {pack}", "y", f"/tools load {pack}", "/quit"],
            sim)
        self.assertEqual(rc, 0)
        self.assertIn("pack 'tpack' registered", log)
        self.assertIn("already registered", log)


class AssembleManifestSourceTests(unittest.TestCase):
    """assemble_manifest_from_source reconstructs a pack's manifest JSON
    from its VAR = [[...]] / VAR = VAR .. [[...]] lines, host-side, so it
    can be validated before any device line is sent (H6.1 AC#2)."""

    def _demo_src(self):
        path = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                            "..", "..", "host_app", "tool_packs", "demo.lua")
        with open(path, encoding="utf-8") as f:
            return f.read()

    def test_assembles_real_demo_pack(self):
        text, err = assistant.assemble_manifest_from_source(self._demo_src())
        self.assertIsNone(err, err)
        m, verr = assistant.validate_manifest(text)
        self.assertIsNone(verr, verr)
        self.assertEqual(m["name"], "demo")
        self.assertEqual([t["name"] for t in m["tools"]],
                         ["mean", "temp_convert", "bench_reset"])

    def test_no_manifest_function(self):
        text, err = assistant.assemble_manifest_from_source(
            "function mean(a) return 'x' end\n")
        self.assertIsNone(text)
        self.assertIn("manifest", err)

    def test_no_string_literal(self):
        text, err = assistant.assemble_manifest_from_source("\n".join([
            "function manifest() return built_elsewhere end",
            "built_elsewhere = 'not a long string'",
        ]))
        self.assertIsNone(text)
        self.assertIn("string literal", err)

    def test_multiline_string_falls_back(self):
        # a pack that spans [[...]] across lines (the loose form) is not
        # reconstructible here - returns None so the caller falls back
        text, err = assistant.assemble_manifest_from_source("\n".join([
            'M = [[{"version":1,"name":"x","tools":[{"name":"a","doc":"d"}]',
            '}]]',
            "function manifest() return M end",
        ]))
        self.assertIsNone(text)
        self.assertIsNotNone(err)

    def test_variable_name_agnostic(self):
        text, err = assistant.assemble_manifest_from_source("\n".join([
            'CFG = [[{"version":1,"name":"z","tools":[{"name":"f","doc":"d"}]}]]',
            "function manifest() return CFG end",
        ]))
        self.assertIsNone(err, err)
        m, verr = assistant.validate_manifest(text)
        self.assertIsNone(verr, verr)
        self.assertEqual(m["name"], "z")
        self.assertEqual([t["name"] for t in m["tools"]], ["f"])


class ToolsCommandEdgeTests(unittest.TestCase):
    """Edge branches in the /tools surface: no device, missing pack file,
    and refresh against a device serving an unregistered pack."""

    class ListSerial:
        timeout = 1

        def __init__(self, responses):
            self.responses = list(responses)
            self.written = []

        def write(self, b):
            self.written.append(b)

        def flush(self):
            pass

        def readline(self):
            return self.responses.pop(0) if self.responses else b""

        def reset_input_buffer(self):
            pass

    def _sess(self, serial=None):
        sess = assistant.Session.__new__(assistant.Session)
        sess.tee = unittest.mock.Mock()
        sess.buf = assistant.DeviceBuffer()
        sess.tools = assistant.ToolRegistry()
        sess.s = serial
        return sess

    def _register(self, sess, name="tpack"):
        m, _ = assistant.validate_manifest(
            '{"version":1,"name":"%s","tools":[{"name":"mean","doc":"d",'
            '"args":[{"name":"numbers","type":"table","item_type":"number"}],'
            '"returns":"string"}]}' % name)
        sess.tools.add(m, name + ".lua")

    def test_load_no_device(self):
        sess = self._sess(None)
        sess.do_tools_load("demo.lua")
        self.assertIn("no device", str(sess.tee.say.call_args))

    def test_load_file_not_found(self):
        sess = self._sess(unittest.mock.Mock())
        sess.do_tools_load("/nonexistent/pack.lua")
        self.assertIn("pack file not found", str(sess.tee.say.call_args))

    def test_refresh_no_device(self):
        sess = self._sess(None)
        self._register(sess)
        sess.do_tools_refresh()
        self.assertIn("no device", str(sess.tee.say.call_args))

    def test_refresh_serves_unknown_pack(self):
        other = ('{"version":1,"name":"otherpack",'
                 '"tools":[{"name":"sum","doc":"d"}]}')
        responses = [(
            '{"status":"ok","cmd":"lua_exec","result":' + json.dumps(other)
            + '}\n').encode()]
        sess = self._sess(self.ListSerial(responses))
        self._register(sess)      # host has "tpack"; device serves "otherpack"
        sess.do_tools_refresh()
        self.assertIn("not in the registry",
                      str(sess.tee.say.call_args_list))


class ArgSigTests(unittest.TestCase):
    """The manifest arg/format helpers, tested directly (previously only
    exercised through validate_manifest / describe / prompt)."""

    def test_check_arg_valid(self):
        for a in ({"name": "x", "type": "number"},
                  {"name": "t", "type": "table", "item_type": "number"},
                  {"name": "u", "type": "string", "enum": ["c", "f"]}):
            self.assertIsNone(assistant._check_arg(a), a)

    def test_check_arg_rejections(self):
        for a, key in [
            ({"type": "number"}, "name"),                  # missing name
            ({"name": "1bad", "type": "number"}, "name"),  # not Lua-safe
            ({"name": "x", "type": "float"}, "type"),      # unknown type
            ({"name": "x", "type": "table",
              "item_type": "float"}, "item_type"),         # bad item_type
        ]:
            err = assistant._check_arg(a)
            self.assertIsNotNone(err, a)
            self.assertIn(key, err, a)

    def test_arg_sig_with_enum(self):
        t = {"name": "temp_convert",
             "args": [{"name": "value", "type": "number"},
                      {"name": "unit", "type": "string", "enum": ["c", "f"]}]}
        self.assertEqual(assistant._arg_sig(t),
                         "temp_convert(value: number, unit: string (c|f))")

    def test_arg_sig_empty(self):
        self.assertEqual(
            assistant._arg_sig({"name": "bench_reset", "args": []}),
            "bench_reset()")

    def test_arg_call_sig(self):
        t = {"name": "temp_convert",
             "example": {"args": {"value": 100, "unit": "c"}}}
        self.assertEqual(assistant._arg_call_sig(t),
                         'temp_convert({value=100, unit="c"})')
        self.assertEqual(assistant._arg_call_sig({"name": "bench_reset"}),
                         "bench_reset({})")


class ValidateManifestEdgeTests(unittest.TestCase):
    def test_manifest_not_object(self):
        m, err = assistant.validate_manifest("[1,2,3]")
        self.assertIsNone(m)
        self.assertIn("not a JSON object", err)

    def test_version_must_be_int(self):
        m, err = assistant.validate_manifest(
            '{"version":"1","name":"x","tools":[{"name":"a","doc":"d"}]}')
        self.assertIsNone(m)
        self.assertIn("version", err)

    def test_mutating_must_be_bool(self):
        m, err = assistant.validate_manifest(
            '{"version":1,"name":"x","tools":[{"name":"a","doc":"d",'
            '"mutating":"yes"}]}')
        self.assertIsNone(m)
        self.assertIn("mutating", err)

    def test_arg_missing_name(self):
        m, err = assistant.validate_manifest(
            '{"version":1,"name":"x","tools":[{"name":"a","doc":"d",'
            '"args":[{"type":"number"}]}]}')
        self.assertIsNone(m)
        self.assertIn("name", err)


class LuaExecLinesKindTests(unittest.TestCase):
    """lua_exec_lines with kind="tool program" (the error strings name the
    source kind; the pack path already covers the default)."""

    def test_tool_program_kind_in_error(self):
        lines, err = assistant.lua_exec_lines(
            "t = os.time()", kind="tool program")
        self.assertIsNone(lines)
        self.assertIn("tool program rejected", err)
        self.assertIn("os.", err)

    def test_tool_program_max_lines(self):
        lines, err = assistant.lua_exec_lines(
            "return 1\nreturn 2", kind="tool program", max_lines=1)
        self.assertIsNone(lines)
        self.assertIn("more than 1", err)


class ExecToolProgramTests(unittest.TestCase):
    """exec_tool_program (generate-and-execute) at unit level: multi-line
    programs, the last line's return is the result, no device, and a
    mid-program device error."""

    class ListSerial:
        timeout = 1

        def __init__(self, responses):
            self.responses = list(responses)
            self.written = []

        def write(self, b):
            self.written.append(b)

        def flush(self):
            pass

        def readline(self):
            return self.responses.pop(0) if self.responses else b""

        def reset_input_buffer(self):
            pass

    @staticmethod
    def lua_ok(result):
        return ('{"status":"ok","cmd":"lua_exec","result":' + json.dumps(result)
                + '}\n').encode()

    @staticmethod
    def begin_ok():
        return b'{"status":"ok","cmd":"lua_begin","msg":"ready"}\n'

    @staticmethod
    def end_ok(result):
        return ('{"status":"ok","cmd":"lua_end","result":'
                + json.dumps(result) + '}\n').encode()

    def _sess(self, serial):
        sess = assistant.Session.__new__(assistant.Session)
        sess.tee = unittest.mock.Mock()
        sess.buf = assistant.DeviceBuffer()
        sess.s = serial
        return sess

    def test_multi_line_uploads_as_one_chunk(self):
        """M3: a multi-line program goes through LUA BEGIN/END so locals
        persist across lines — the wire is BEGIN + raw lines + END. The
        b"" entries model the read timeout after each silently-acked
        data line."""
        sim = self.ListSerial([self.begin_ok(), b"", b"",
                               self.end_ok("6.00")])
        ok, result = self._sess(sim).exec_tool_program(
            "local a = 1\nreturn mean({numbers={3,5,10}})")
        self.assertTrue(ok)
        self.assertEqual(result, "6.00")
        sent = [w.decode().strip() for w in sim.written]
        self.assertIn("LUA BEGIN", sent)
        self.assertIn("local a = 1", sent)          # raw line, no prefix
        self.assertIn("return mean({numbers={3,5,10}})", sent)
        self.assertIn("LUA END", sent)

    def test_single_line_still_direct_exec(self):
        sim = self.ListSerial([self.lua_ok("7")])
        ok, result = self._sess(sim).exec_tool_program("return 7")
        self.assertTrue(ok)
        self.assertEqual(result, "7")
        sent = [w.decode().strip() for w in sim.written]
        self.assertEqual(sent, ["LUA EXEC return 7"])

    def test_no_device(self):
        ok, result = self._sess(None).exec_tool_program("return 1")
        self.assertFalse(ok)
        self.assertEqual(result, "no device connected")

    def test_device_rejects_line(self):
        err = ('{"status":"error","cmd":"lua_exec","code":-630,'
               '"msg":"attempt to call a nil value"}\n').encode()
        ok, result = self._sess(self.ListSerial([err])).exec_tool_program(
            "return mean({numbers={1,2,3}})")
        self.assertFalse(ok)
        self.assertIn("device rejected", result)

    def test_chunk_rejection_surfaces(self):
        """exec_tool_program host-scans first with the SAME token list,
        so the DEVICE-side mid-chunk rejection is exercised at the
        lua_exec_chunk level directly (upload layer, one line at a
        time)."""
        err = ('{"status":"error","cmd":"lua_data","code":-612,'
               '"msg":"sandbox violation"}\n').encode()
        ok, detail = assistant.lua_exec_chunk(
            self.ListSerial([self.begin_ok(), err]), ["t = os.time()"])
        self.assertFalse(ok)
        self.assertIn("-612", detail)


# ---- H6.1 M2: pack persistence host surface ----------------------------------

class UploadPackTests(unittest.TestCase):
    """upload_pack mirrors upload_script: silent data acks, PACK BEGIN
    carries the autorun flag, a violating line aborts with -612 and no
    PACK END is attempted."""

    PACK_BEGIN_OK = (b'{"status":"ok","cmd":"pack_begin",'
                     b'"msg":"ready","autorun":false}\n')
    PACK_END_OK = b'{"status":"ok","cmd":"pack_end","size":7}\n'
    REJECT = (b'{"status":"error","cmd":"pack_data","code":-612,'
              b'"msg":"sandbox violation: \'os.\' is not allowed"}\n')

    def sent(self, sim):
        return [w.decode().strip() for w in sim.written]

    def test_clean_upload(self):
        sim = FetchManifestTests.FakeSerial(
            [self.PACK_BEGIN_OK, b"", self.PACK_END_OK])
        ok, detail = assistant.upload_pack(sim, "demo", "a1 = 1", False,
                                           None)
        self.assertTrue(ok)
        self.assertEqual(detail, "stored")
        self.assertEqual(self.sent(sim),
                         ["PACK BEGIN demo", "a1 = 1", "PACK END"])

    def test_autorun_flag_on_begin(self):
        sim = FetchManifestTests.FakeSerial(
            [self.PACK_BEGIN_OK, b"", self.PACK_END_OK])
        assistant.upload_pack(sim, "demo", "a1 = 1", True, None)
        self.assertIn("PACK BEGIN demo autorun", self.sent(sim))

    def test_forbidden_line_surfaces_minus_612(self):
        sim = FetchManifestTests.FakeSerial(
            [self.PACK_BEGIN_OK, self.REJECT])
        ok, detail = assistant.upload_pack(sim, "demo", "t = os.time()",
                                           False, None)
        self.assertFalse(ok)
        self.assertIn("-612", detail)
        # aborted at the offending line: no PACK END attempt
        self.assertNotIn("PACK END", self.sent(sim))

    def test_expected_cmd_maps_pack_family(self):
        self.assertEqual(assistant.expected_cmd("PACK BEGIN demo"),
                         "pack_begin")
        self.assertEqual(assistant.expected_cmd("PACK LIST"), "pack_list")
        self.assertEqual(assistant.expected_cmd("PACK RUN x"), "pack_run")


class PackReplTests(ToolReplTests):
    """M2 session flows: /tools persist (wire = PACK BEGIN/END with raw
    text lines), /tools load @name activating device storage, /tools
    showing device packs, unknown @pack guidance."""

    @staticmethod
    def jline(obj):
        return (json.dumps(obj, separators=(",", ":")) + "\n").encode()

    def m2_responses(self):
        r = dict(self.lua_pack_responses())
        begin = self.jline({"status": "ok", "cmd": "pack_begin",
                            "msg": "ready", "autorun": True})
        r["PACK BEGIN tpack"] = [begin]
        r["PACK BEGIN tpack autorun"] = [begin]
        r["PACK END"] = [self.jline(
            {"status": "ok", "cmd": "pack_end", "size": 118})]
        r["PACK LIST"] = [self.jline(
            {"status": "ok", "cmd": "pack_list",
             "packs": [{"name": "tpack", "size": 118, "autorun": True}],
             "free": 30000})]
        r["PACK RUN tpack"] = [self.jline(
            {"status": "ok", "cmd": "pack_run", "result": ""})]
        return r

    def test_tools_persist_flow(self):
        sim = DeviceSimSerial(self.m2_responses())
        pack = self.write_pack()
        rc, log = self.run_session(
            [f"/tools load {pack}", "y",
             "/tools persist tpack autorun", "y", "/quit"], sim)
        self.assertEqual(rc, 0)
        self.assertIn("persist pack 'tpack' to device storage", log)
        self.assertIn("and run it at every boot", log)
        self.assertIn("confirm> y", log)
        self.assertIn("pack 'tpack' stored on the device", log)
        self.assertIn("autorun on", log)
        # the wire: PACK BEGIN with the flag, raw text lines (no LUA
        # EXEC prefix - the device scans them itself), PACK END
        self.assertIn("PACK BEGIN tpack autorun", sim.written)
        self.assertIn("function manifest() return TM end", sim.written)
        self.assertIn("PACK END", sim.written)

    def test_tools_persist_declined(self):
        sim = DeviceSimSerial(self.m2_responses())
        pack = self.write_pack()
        rc, log = self.run_session(
            [f"/tools load {pack}", "y",
             "/tools persist tpack", "n", "/quit"], sim)
        self.assertEqual(rc, 0)
        self.assertIn("(aborted - nothing was sent)", log)
        self.assertFalse([w for w in sim.written
                          if w.startswith("PACK BEGIN")])

    def test_tools_load_at_name_registers_from_device(self):
        sim = DeviceSimSerial(self.m2_responses())
        rc, log = self.run_session(
            ["/tools load @tpack", "y", "/tools", "/quit"], sim)
        self.assertEqual(rc, 0)
        self.assertIn("activate stored pack 'tpack'", log)
        self.assertIn("confirm> y", log)
        self.assertIn("pack 'tpack' registered: 1 tools, "
                      "from device storage", log)
        self.assertIn("PACK RUN tpack", sim.written)
        self.assertIn("device packs (persisted): tpack (autorun) "
                      "[active]", log)

    def test_tools_at_unknown_pack_guides(self):
        sim = DeviceSimSerial(self.m2_responses())
        rc, log = self.run_session(
            ["/tools load @ghost", "/quit"], sim)
        self.assertEqual(rc, 0)
        self.assertIn("no such device pack; stored: tpack", log)
        self.assertNotIn("PACK RUN ghost", sim.written)


# ---- H6.1 M3: mutating gate + hwio pack --------------------------------------

class MutatingGateTests(unittest.TestCase):
    """--mutating-gate (decision 10's M3 host-policy return): a program
    calling a manifest-declared mutating tool needs an explicit y before
    it executes; declining feeds the LLM a corrective message and does
    NOT consume the execution budget."""

    def setUp(self):
        self.sess = assistant.Session.__new__(assistant.Session)
        self.sess.tee = unittest.mock.Mock()
        self.sess.buf = assistant.DeviceBuffer()
        self.sess.history = assistant.History()
        self.sess.cfg = {"base": "https://x/v1", "key": "k", "model": "m"}
        self.sess.system_prompt = assistant.SYSTEM_PROMPT
        self.sess.mutating_gate = True
        self.sess.s = None
        m, _ = assistant.validate_manifest(DEMO_MANIFEST)
        self.sess.tools = assistant.ToolRegistry()
        self.sess.tools.add(m, "demo.lua")

    @staticmethod
    def program():
        return json.dumps({"type": "lua", "text": "reset",
                           "code": "return bench_reset({})"})

    def said(self):
        return " ".join(str(c.args[0])
                        for c in self.sess.tee.say.call_args_list
                        if c.args)

    def test_names_detected_from_manifest(self):
        self.assertEqual(self.sess.mutating_names_in(
            "return bench_reset({})"), ["bench_reset"])
        self.assertEqual(self.sess.mutating_names_in(
            "x = bench_reset\nreturn mean({numbers={1}})"),
            [])                        # bare reference, no call paren
        self.assertEqual(self.sess.mutating_names_in(
            "return mean({numbers={1}})"), [])

    def test_gate_declined_blocks_execution(self):
        self.sess.reader = unittest.mock.Mock()
        self.sess.reader.poll_line.side_effect = ["n", ""]
        replies = [self.program(),
                   '{"type":"answer","text":"ok without reset"}']
        it = iter(replies)
        with unittest.mock.patch.object(
                assistant, "llm_chat",
                side_effect=lambda *a, **k: next(it)) as chat:
            self.sess.ask_llm("reset the bench")
        self.assertEqual(chat.call_count, 2)
        said = self.said()
        self.assertIn("mutating tool(s): bench_reset", said)
        self.assertIn("execute? [y/N]", said)
        self.assertIn("(declined - nothing was executed)", said)
        self.assertIn("llm: ok without reset", said)

    def test_gate_confirmed_runs(self):
        self.sess.reader = unittest.mock.Mock()
        self.sess.reader.poll_line.side_effect = ["y", ""]
        replies = [self.program(),
                   '{"type":"answer","text":"done"}']
        it = iter(replies)
        with unittest.mock.patch.object(
                assistant, "llm_chat",
                side_effect=lambda *a, **k: next(it)):
            self.sess.ask_llm("reset the bench")
        said = self.said()
        self.assertIn("confirm> y", said)
        # device is None here, so the confirmed run surfaces as a clean
        # execution failure fed back to the LLM - the GATE let it through
        self.assertNotIn("(declined", said)
        self.assertIn("llm: done", said)

    def test_gate_off_by_default(self):
        self.sess.mutating_gate = False
        self.sess.reader = unittest.mock.Mock()
        self.sess.reader.poll_line.return_value = ""
        replies = [self.program(),
                   '{"type":"answer","text":"done"}']
        it = iter(replies)
        with unittest.mock.patch.object(
                assistant, "llm_chat",
                side_effect=lambda *a, **k: next(it)):
            self.sess.ask_llm("reset the bench")
        self.assertNotIn("execute? [y/N]", self.said())


class HwioPackTests(unittest.TestCase):
    """The shipped hwio pack obeys the convention and its manifest
    assembles + validates (same replay guard that pinned the demo pack
    after the 2026-09-05 stray-brace bug)."""

    PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                        "..", "..", "host_app", "tool_packs", "hwio.lua")

    def setUp(self):
        with open(self.PATH, encoding="utf-8") as f:
            self.src = f.read()

    def test_lines_within_convention(self):
        lines, err = assistant.lua_exec_lines(self.src)
        self.assertIsNone(err, err)
        self.assertTrue(all(len(l) <= assistant.LUA_EXEC_LINE_MAX
                            for l in lines))
        self.assertEqual(len([l for l in lines
                              if l.startswith("function ")]), 7)

    def test_manifest_assembles_valid(self):
        text, aerr = assistant.assemble_manifest_from_source(self.src)
        self.assertIsNone(aerr, aerr)
        m, verr = assistant.validate_manifest(text)
        self.assertIsNone(verr, verr)
        self.assertEqual(m["name"], "hwio")
        self.assertEqual([t["name"] for t in m["tools"]],
                         ["uptime_ms", "pin_read", "pin_write", "adc_raw",
                          "cfg_set", "cfg_get"])
        mut = [t["name"] for t in m["tools"] if t.get("mutating")]
        self.assertEqual(mut, ["pin_write", "cfg_set"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
