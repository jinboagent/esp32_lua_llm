"""H6.1 M2 hardware checks: PACK storage + boot autorun (COM12).

Verifies on the real device:
  - PACK upload (fail-closed scan on-device, -612 mid-upload)
  - storage is inert: a stored pack does NOT execute until PACK RUN
  - PACK RUN works while scanning (outside the CLI state machine)
  - THE M2 PROOF: reboot (COM close resets the chip, N3) -> firmware
    executes autorun-marked packs at boot -> manifest() answers with no
    host-side load, on any PC
  - PACK DEL removes file+marker; boot autorun then finds nothing
  - PACK LIST shape (name/size/autorun/free)
  - audit B3: markers+packs past the old 8-dirent cap stay visible in
    LIST and still autorun at boot; a 9th pack is rejected (-624)

Run:  python tests/hw/test_pack_hw.py [port]
"""
import json
import os
import sys
import time

import serial

PORT = sys.argv[1] if len(sys.argv) > 1 else "COM12"
PACK_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                         "..", "..", "host_app", "tool_packs", "demo.lua")

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


def upload_pack(s, name, lines, autorun=False):
    r = cmd(s, "PACK BEGIN " + name + (" autorun" if autorun else ""))
    if not (r and r.get("status") == "ok"):
        return False, json.dumps(r)
    for ln in lines:
        s.write((ln + "\n").encode())
        s.flush()
        time.sleep(0.05)
        while True:
            raw = s.readline()
            if not raw:
                break
            txt = raw.decode(errors="replace").strip()
            if txt.startswith("{") and '"status":"error"' in txt:
                return False, txt
    time.sleep(0.3)
    r = cmd(s, "PACK END")
    if not (r and r.get("status") == "ok"):
        return False, json.dumps(r)
    return True, r.get("size")


def fetch_manifest_head(s):
    """manifest() via the chunked exec path; returns the first chunk."""
    return cmd(s, "LUA EXEC return string.sub(manifest(),1,120)")


def boot_banner(s, secs=4.0):
    end = time.time() + secs
    lines = []
    while time.time() < end:
        raw = s.readline()
        if raw:
            lines.append(raw.decode(errors="replace").strip())
    return lines


def main():
    with open(PACK_FILE, encoding="utf-8") as f:
        pack_lines = [ln.rstrip() for ln in f
                      if ln.strip() and not ln.strip().startswith("--")]

    s = serial.Serial(PORT, 115200, timeout=1)
    time.sleep(0.8)
    boot_banner(s, 2.0)
    print(f"=== H6.1 M2 pack hardware checks on {PORT} ===")

    # clean slate: remove a leftover demo pack from an earlier run, and
    # reset the live Lua state (a prior session's boot autorun may have
    # defined the globals — PACK DEL removes storage, not live globals)
    r = cmd(s, "PACK LIST")
    if r and any(p.get("name") == "demo" for p in r.get("packs", [])):
        cmd(s, "PACK DEL demo")
    cmd(s, "LUA DEINIT")
    r = cmd(s, "LUA INIT")
    check("Lua state reset (LUA DEINIT+INIT)",
          bool(r and r.get("status") == "ok"), json.dumps(r))

    # 1) upload with autorun
    ok, detail = upload_pack(s, "demo", pack_lines, autorun=True)
    check("PACK upload (autorun) accepted", ok, str(detail))

    # 2) LIST shows it with the marker
    r = cmd(s, "PACK LIST")
    packs = (r or {}).get("packs", [])
    entry = next((p for p in packs if p.get("name") == "demo"), None)
    check("PACK LIST shows demo", entry is not None)
    check("autorun flag true", bool(entry and entry.get("autorun")))

    # 3) storage is inert: manifest() must NOT answer before PACK RUN
    r = fetch_manifest_head(s)
    inert = not (r and r.get("status") == "ok")
    check("stored pack does not execute (manifest absent)", inert,
          json.dumps(r))

    # 4) PACK RUN defines the globals; manifest serves
    r = cmd(s, "PACK RUN demo")
    check("PACK RUN ok", bool(r and r.get("status") == "ok"),
          json.dumps(r))
    r = fetch_manifest_head(s)
    head = r.get("result", "") if r else ""
    check("manifest() served after PACK RUN",
          bool(r and r.get("status") == "ok") and '"version":1' in head,
          head[:60])

    # 5) packs live outside the state machine: RUN while scanning
    cmd(s, "SCAN START")
    time.sleep(1.0)
    r = cmd(s, "PACK RUN demo")
    check("PACK RUN while scanning (outside state machine)",
          bool(r and r.get("status") == "ok"), json.dumps(r))
    cmd(s, "SCAN STOP")

    # 6) THE M2 PROOF — reboot (port close resets the chip) and the
    #    pack must already be alive, with no host-side load. The boot
    #    banner itself prints before the console re-attaches (the port
    #    is closed during the reset), so the PROOF is the manifest
    #    answering immediately after reopen; the banner is informational.
    s.close()
    time.sleep(2.5)
    s = serial.Serial(PORT, 115200, timeout=1)
    time.sleep(0.8)
    banner = boot_banner(s)
    print(f"  (boot banner lines seen after reopen: {banner[:4]})")
    r = fetch_manifest_head(s)
    head = r.get("result", "") if r else ""
    check("manifest() alive after reboot with NO host load",
          bool(r and r.get("status") == "ok") and '"version":1' in head,
          head[:60])

    # 5b) audit B3: packs + autorun markers occupy one dirent EACH; with
    #     the old 8-dirent read cap, packs silently vanished from LIST and
    #     from boot autorun. Here: demo + 6 packs + 4 markers = 12 files in
    #     the packs dir; LIST must show every pack, the store cap must
    #     reject a 9th pack (-624), and after a reboot all marked packs
    #     must be alive.
    b3_names = ["b3a", "b3b", "b3c", "b3d", "b3e", "b3f"]
    marked = {"b3a", "b3c", "b3f"}
    for n in b3_names:
        body = [n + "_ran = 1"] if n in marked else [n + "_idle = 1"]
        ok, detail = upload_pack(s, n, body, autorun=n in marked)
        check("B3 upload " + n, ok, str(detail))
    r = cmd(s, "PACK LIST")
    listed = {p.get("name") for p in (r or {}).get("packs", [])}
    check("B3: LIST shows all packs (dirents past the old cap)",
          set(b3_names) <= listed, json.dumps(r))
    ok, detail = upload_pack(s, "b3g", ["g = 1"])   # 8th pack: fits
    check("B3: 8th pack still accepted", ok, str(detail))
    r = cmd(s, "PACK BEGIN b3h")
    check("B3: 9th pack rejected (-624 store full)",
          bool(r and r.get("status") == "error"
               and r.get("code") == -624), json.dumps(r))
    cmd(s, "PACK DEL b3g")
    s.close()
    time.sleep(2.5)
    s = serial.Serial(PORT, 115200, timeout=1)
    time.sleep(0.8)
    boot_banner(s)
    for n in sorted(marked):
        r = cmd(s, "LUA EXEC return tostring(" + n + "_ran)")
        check("B3: " + n + " alive after reboot (autorun past cap)",
              bool(r and r.get("status") == "ok"
                   and r.get("result") == "1"), json.dumps(r))
    for n in b3_names:
        cmd(s, "PACK DEL " + n)

    # 7) forbidden line rejected on-device, mid-upload
    ok, detail = upload_pack(s, "bad", ["t = os.time()"])
    check("forbidden pack line rejected (-612)",
          (not ok) and "-612" in str(detail), str(detail))

    # 8) DEL removes file+marker; the next boot finds nothing
    r = cmd(s, "PACK DEL demo")
    check("PACK DEL ok", bool(r and r.get("status") == "ok"))
    r = cmd(s, "PACK LIST")
    packs = (r or {}).get("packs", [])
    check("LIST empty after DEL", not any(p.get("name") == "demo"
                                          for p in packs))
    s.close()
    time.sleep(2.5)
    s = serial.Serial(PORT, 115200, timeout=1)
    time.sleep(0.8)
    boot_banner(s)
    r = fetch_manifest_head(s)
    check("manifest() gone after DEL+reboot",
          not (r and r.get("status") == "ok"))

    s.close()
    print(f"=== {passed} passed, {failed} failed ===")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
