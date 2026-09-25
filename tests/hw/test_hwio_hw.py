"""H6.1 M3 hardware checks: hw.* bindings + LUA chunk exec (COM12).

Verifies on the real device:
  - the hw table exists and millis() advances
  - gpio write/read round-trip on a whitelisted pin; the USB pin (19)
    is refused (fail-closed whitelist)
  - ADC1 raw read on a valid gpio
  - kv store set/get round-trip — and THE M3 CONFIGURE-MODE PROOF: the
    value survives a reboot (port close resets the chip), because the
    DEVICE owns the parameters now
  - LUA BEGIN/END chunk: locals persist across lines within one chunk
  - a violating chunk line is rejected mid-upload with -612
  - the hwio pack wraps it all behind the tool convention

Run:  python tests/hw/test_hwio_hw.py [port]
"""
import json
import os
import sys
import time

import serial

PORT = sys.argv[1] if len(sys.argv) > 1 else "COM12"
PACK_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                         "..", "..", "host_app", "tool_packs", "hwio.lua")

passed = 0
failed = 0


def check(name, cond, detail=""):
    global passed, failed
    mark = "PASS" if cond else "FAIL"
    if cond:
        passed += 1
    else:
        failed += 1
    print(f"[{mark}] {name}" + (f"  ({detail})" if detail and not cond
                                else ""))


def expected_cmd(line):
    words = line.split()
    if len(words) >= 2 and words[0] in ("SCAN", "CONN", "SCRIPT", "POWER",
                                        "LUA", "PACK"):
        return (words[0] + "_" + words[1]).lower()
    return None


def cmd(s, line, timeout=4.0):
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
            except ValueError:
                continue
            if isinstance(obj, dict) and (want is None
                                          or obj.get("cmd") == want):
                return obj
    return None


def drain(s, secs=2.0):
    end = time.time() + secs
    while time.time() < end:
        if s.readline():
            continue


def main():
    with open(PACK_FILE, encoding="utf-8") as f:
        pack_lines = [ln.rstrip() for ln in f
                      if ln.strip() and not ln.strip().startswith("--")]

    s = serial.Serial(PORT, 115200, timeout=1)
    time.sleep(0.8)
    drain(s, 2.0)
    print(f"=== H6.1 M3 hw.* + chunk hardware checks on {PORT} ===")

    cmd(s, "LUA DEINIT")
    cmd(s, "LUA INIT")
    drain(s, 0.5)

    # -- hw table + millis ------------------------------------------------
    r = cmd(s, "LUA EXEC return type(hw)")
    check("hw table present (CONFIG_LUA_HW_BINDINGS=y)",
          bool(r and r.get("result") == "table"), json.dumps(r))
    r1 = cmd(s, "LUA EXEC return hw.millis()")
    time.sleep(0.2)
    r2 = cmd(s, "LUA EXEC return hw.millis()")
    m1 = int(r1.get("result", "0")) if r1 else 0
    m2 = int(r2.get("result", "-1")) if r2 else -1
    check("millis() advances", 0 <= m1 and m2 > m1, f"{m1} -> {m2}")

    # -- gpio round-trip + whitelist --------------------------------------
    r = cmd(s, "LUA EXEC return hw.gpio_write(4, 1)")
    check("gpio_write(4,1) ok",
          bool(r and r.get("status") == "ok"), json.dumps(r))
    r = cmd(s, "LUA EXEC return hw.gpio_read(4)")
    check("gpio_read(4) returns the driven level",
          bool(r and r.get("result") == "1"), json.dumps(r))
    cmd(s, "LUA EXEC return hw.gpio_write(4, 0)")
    r = cmd(s, "LUA EXEC return hw.gpio_read(19)")
    check("USB pin 19 refused (fail-closed whitelist)",
          bool(r and r.get("status") == "error"
               and "not whitelisted" in r.get("msg", "")), json.dumps(r))
    # audit B12: strapping pins (3, 45, 46) and UART0 (43/44) stay out of
    # Lua's reach even though they sit inside the old numeric ranges.
    for pin in (3, 43, 45, 46):
        r = cmd(s, f"LUA EXEC return hw.gpio_read({pin})")
        check(f"strap/UART pin {pin} refused (B12)",
              bool(r and r.get("status") == "error"
                   and "not whitelisted" in r.get("msg", "")),
              json.dumps(r))

    # -- adc ---------------------------------------------------------------
    r = cmd(s, "LUA EXEC return hw.adc_read(4)")
    try:
        raw = int(r.get("result", "-1"))
    except (ValueError, AttributeError):
        raw = -1
    check("adc_read(4) raw in 0..4095", 0 <= raw <= 4095,
          json.dumps(r))
    r = cmd(s, "LUA EXEC return hw.adc_read(11)")
    check("adc outside gpio1..10 refused",
          bool(r and r.get("status") == "error"), json.dumps(r))

    # -- kv round-trip ------------------------------------------------------
    r = cmd(s, 'LUA EXEC return hw.kv_set("m3t", "hello")')
    check("kv_set ok", bool(r and r.get("status") == "ok"),
          json.dumps(r))
    r = cmd(s, 'LUA EXEC return hw.kv_get("m3t")')
    check("kv_get round-trips", bool(r and r.get("result") == "hello"),
          json.dumps(r))
    r = cmd(s, 'LUA EXEC return tostring(hw.kv_get("absent_x") == nil)')
    check("absent key reads nil", bool(r and r.get("result") == "true"),
          json.dumps(r))

    # -- chunk exec: locals persist within one chunk ------------------------
    cmd(s, "LUA BEGIN")
    s.write(b"local x = 21\n")
    s.flush()
    time.sleep(0.1)
    s.write(b"return x * 2\n")
    s.flush()
    time.sleep(0.3)
    r = cmd(s, "LUA END")
    check("chunk: locals persist across lines (21*2)",
          bool(r and r.get("status") == "ok" and r.get("result") == "42"),
          json.dumps(r))

    # -- chunk violation aborts mid-upload ----------------------------------
    cmd(s, "LUA BEGIN")
    s.write(b"t = os.time()\n")
    s.flush()
    time.sleep(0.3)
    end = time.time() + 2.0
    violation = None
    while time.time() < end:
        raw = s.readline()
        if not raw:
            continue
        txt = raw.decode(errors="replace").strip()
        if txt.startswith("{") and "-612" in txt:
            violation = json.loads(txt)
            break
    check("chunk forbidden line -> -612 mid-upload",
          bool(violation and violation.get("cmd") == "lua_data"),
          str(violation))

    # -- hwio pack end to end ------------------------------------------------
    cmd(s, "PACK DEL hwio")
    r = cmd(s, "PACK BEGIN hwio")
    if r and r.get("status") == "ok":
        for ln in pack_lines:
            s.write((ln + "\n").encode())
            s.flush()
            time.sleep(0.05)
        time.sleep(0.3)
        r = cmd(s, "PACK END")
        check("hwio pack stored", bool(r and r.get("status") == "ok"),
              json.dumps(r))
    r = cmd(s, "PACK RUN hwio")
    check("hwio pack runs", bool(r and r.get("status") == "ok"),
          json.dumps(r))
    r = cmd(s, "LUA EXEC return uptime_ms({})")
    check("tool uptime_ms() works",
          bool(r and r.get("status") == "ok"
               and r.get("result", "").endswith(" ms")), json.dumps(r))
    r = cmd(s, 'LUA EXEC return cfg_set({key="label", value="bench3"})')
    check("tool cfg_set works",
          bool(r and r.get("result") == "ok: label=bench3"),
          json.dumps(r))

    # -- THE M3 CONFIGURE-MODE PROOF: reboot, the value survives ------------
    # The raw device API answers immediately (the device owns the value);
    # the pack's tool needs PACK RUN first — pack globals are
    # session-scoped by design (decision 13), the PARAMETERS are not.
    s.close()
    time.sleep(2.5)
    s = serial.Serial(PORT, 115200, timeout=1)
    time.sleep(0.8)
    drain(s, 2.0)
    r = cmd(s, 'LUA EXEC return hw.kv_get("label")')
    check("CONFIGURE PROOF: raw kv value survives reboot",
          bool(r and r.get("result") == "bench3"), json.dumps(r))
    cmd(s, "PACK RUN hwio")
    r = cmd(s, 'LUA EXEC return cfg_get({key="label"})')
    check("CONFIGURE PROOF: tool reads the survived value",
          bool(r and r.get("result") == "label=bench3"), json.dumps(r))

    # cleanup: remove the test pack (kv values stay - that is the point)
    cmd(s, "PACK DEL hwio")

    s.close()
    print(f"=== {passed} passed, {failed} failed ===")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
