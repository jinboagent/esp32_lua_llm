// ESP32 Lua LLM — architecture deck builder (pptxgenjs)
// Run: node build_deck.js   →  ESP32_Lua_LLM_Architecture.pptx
const pptxgen = require("pptxgenjs");

const p = new pptxgen();
p.layout = "LAYOUT_WIDE"; // 13.33 x 7.5
p.author = "bo li";
p.title = "ESP32 Lua LLM Dongle — Architecture";
p.subject = "High-level architecture walkthrough";

// ---------- palette / fonts ----------
const INK = "0F1720";      // dark background
const INK2 = "1A2836";     // dark panel
const PANEL = "F1F5F7";    // light panel
const TINT = "E8F3F6";     // primary tint
const ATINT = "FDEFE7";    // accent tint
const PRIMARY = "0E7490";  // deep cyan-teal
const PRIMARY_DK = "155E75";
const PRIMARY_LT = "5AA7BC";
const ACCENT = "E8590C";   // burnt orange
const TEXT = "1A2733";
const MUTED = "5B6B78";
const INV = "FFFFFF";
const MUTED_INV = "9FB3C2";
const LINE = "D8E0E6";
const CODE = "CFE3EE";
const CMT = "7C93A3";
const F = "Segoe UI";
const M = "Consolas";

const W = 13.33, RX = 12.83; // canvas, right edge at 0.5 margin
const sh = () => ({ type: "outer", color: "0F1720", blur: 7, offset: 2, angle: 90, opacity: 0.14 });
const bu = () => ({ code: "25B8", indent: 12 });

// ---------- helpers ----------
function header(s, tag, title) {
  s.addText(tag, { x: 0.5, y: 0.34, w: 9, h: 0.3, fontFace: M, fontSize: 12, color: MUTED, charSpacing: 2, margin: 0 });
  s.addText(title, { x: 0.5, y: 0.66, w: 12.33, h: 0.66, fontFace: F, fontSize: 28, bold: true, color: TEXT, margin: 0 });
}
function pageNo(s, n) {
  s.addText(String(n).padStart(2, "0"), { x: 12.35, y: 7.06, w: 0.48, h: 0.3, fontFace: M, fontSize: 12, color: MUTED, align: "right", margin: 0 });
}
function note(s, txt) {
  s.addText(txt, { x: 0.5, y: 7.04, w: 11.6, h: 0.32, fontFace: F, fontSize: 12, color: MUTED, margin: 0 });
}
function node(s, x, y, w, h, txt, o = {}) {
  s.addText(txt, {
    shape: p.shapes.ROUNDED_RECTANGLE, rectRadius: 0.05,
    x, y, w, h, fill: { color: o.fill || INV },
    line: o.lineColor ? { color: o.lineColor, width: o.lineW || 1 } : { color: LINE, width: 0.75 },
    fontFace: o.mono ? M : F, fontSize: o.fs || 12, bold: !!o.bold,
    color: o.color || TEXT, align: "center", valign: "middle", margin: 4,
  });
}
function harrow(s, x1, x2, y, o = {}) { // horizontal arrow x1 -> x2
  s.addShape(p.shapes.LINE, {
    x: x1, y, w: x2 - x1, h: 0,
    line: { color: o.color || PRIMARY_LT, width: o.w || 1.75, endArrowType: o.noEnd ? "none" : "triangle", beginArrowType: o.begin ? "triangle" : "none", dashType: o.dash ? "dash" : "solid" },
  });
}
function vline(s, x, y1, y2, o = {}) {
  s.addShape(p.shapes.LINE, {
    x, y: y1, w: 0, h: y2 - y1,
    line: { color: o.color || PRIMARY_LT, width: o.w || 1.75, endArrowType: o.arrow === false ? "none" : "triangle", dashType: o.dash ? "dash" : "solid" },
  });
}
function hline(s, x1, x2, y, o = {}) {
  s.addShape(p.shapes.LINE, { x: x1, y, w: x2 - x1, h: 0, line: { color: o.color || LINE, width: o.w || 0.75, dashType: o.dash ? "dash" : "solid" } });
}
function label(s, x, y, w, txt, o = {}) {
  s.addText(txt, { x, y, w, h: o.h || 0.3, fontFace: o.mono ? M : F, fontSize: o.fs || 12, bold: !!o.bold, color: o.color || MUTED, align: o.align || "left", margin: 0, charSpacing: o.cs || 0 });
}

// ============================================================
// S1 — TITLE (dark)
// ============================================================
let s = p.addSlide();
s.background = { color: INK };
label(s, 0.62, 0.5, 10, "esp32_lua_llm · architecture walkthrough · 2026-09", { mono: true, fs: 13, color: MUTED_INV });
label(s, 0.62, 1.5, 9.6, "ESP32-S3 · NIMBLE · LUA 5.4 · USB CDC · LLM HOST", { mono: true, fs: 15, color: ACCENT, cs: 2 });
s.addText("A BLE test lab on a USB dongle,\noperated by an LLM.", { x: 0.58, y: 1.95, w: 9.5, h: 1.95, fontFace: F, fontSize: 40, bold: true, color: INV, margin: 0, lineSpacingMultiple: 1.05 });
s.addText("BLE bridging with on-device Lua scripting, and a host-side agent that reads the data, writes its own tools — and ships them to the dongle over USB.", { x: 0.62, y: 4.1, w: 8.9, h: 1.0, fontFace: F, fontSize: 16, color: MUTED_INV, margin: 0, lineSpacingMultiple: 1.15 });
label(s, 0.62, 6.55, 12.2, "v1.1.0 · stages 0–5 complete · stage 6 (agent platform) in progress on Lua_tool_extension_dev", { mono: true, fs: 12.5, color: MUTED_INV });
// mini schematic, right side
node(s, 10.3, 2.0, 2.4, 0.62, "PC — LLM host tools", { fill: INK, lineColor: MUTED_INV, lineW: 1, color: INV, fs: 12 });
vline(s, 11.5, 2.62, 3.02);
label(s, 11.62, 2.66, 1.2, "USB CDC", { mono: true, fs: 12, color: MUTED_INV });
node(s, 10.3, 3.12, 2.4, 0.62, "ESP32-S3 dongle", { fill: INK, lineColor: MUTED_INV, lineW: 1, color: INV, fs: 12 });
vline(s, 11.5, 3.74, 4.14, { dash: true });
label(s, 11.62, 3.78, 1.2, "2.4 GHz", { mono: true, fs: 12, color: MUTED_INV });
node(s, 10.3, 4.24, 2.4, 0.78, "BLE adverts &\nGATT peers", { fill: INK, lineColor: MUTED_INV, lineW: 1, color: INV, fs: 12 });
s.addNotes("One-sentence pitch: a $5 ESP32-S3 bridges BLE devices to the PC and runs Lua; a Python host application puts an LLM in the loop. Everything in this deck is live-verified on hardware. v1.1.0 released; stage 6 (the agent platform) is the current branch.");

// ============================================================
// S2 — WHY
// ============================================================
s = p.addSlide();
s.background = { color: INV };
header(s, "00 · MOTIVATION", "Why this project exists");
const whyRows = [
  ["Getting inside BLE is heavy", "Proprietary capture hardware, Wireshark, and a fresh one-off parser script for every device you meet."],
  ["Firmware logic is frozen", "Every filter tweak or new payload format is a rebuild–reflash–retest cycle on the bench."],
  ["A human sits in every loop", "Someone has to read the capture, spot the pattern, and write the next probe script by hand."],
];
whyRows.forEach((r, i) => {
  const y = 1.72 + i * 1.02;
  if (i > 0) hline(s, 0.5, 6.35, y - 0.14);
  s.addText([
    { text: r[0], options: { bold: true, fontSize: 15.5, color: TEXT, breakLine: true } },
    { text: r[1], options: { fontSize: 13, color: MUTED } },
  ], { x: 0.5, y, w: 5.85, h: 0.9, fontFace: F, margin: 0, paraSpaceAfter: 4, valign: "top" });
});
s.addText([
  { text: "Put a Lua sandbox on a $5 radio — and hand the keyboard to an LLM.", options: { bold: true, fontSize: 20, color: PRIMARY_DK, breakLine: true } },
  { text: "The dongle keeps watch, the model reads the traffic, and new tooling ships over USB in seconds — not reflash cycles.", options: { fontSize: 14, color: TEXT } },
], { x: 6.75, y: 1.75, w: 6.08, h: 2.3, shape: p.shapes.ROUNDED_RECTANGLE, rectRadius: 0.06, fill: { color: TINT }, line: { color: TINT, width: 0 }, fontFace: F, margin: 12, paraSpaceAfter: 10, valign: "middle" });
label(s, 0.5, 4.72, 8, "THE CLOSED LOOP — ALREADY RUNNING LIVE", { mono: true, fs: 12, color: MUTED, cs: 1.5 });
const loop5 = ["capture\nJSON lines", "analyze\nLLM reads", "generate\nLua tool", "deploy\nUSB · sandboxed", "observe\nnext scan"];
loop5.forEach((t, i) => {
  const x = 0.68 + i * 2.44;
  node(s, x, 5.12, 2.2, 0.95, t, { fs: 12.5, fill: i === 2 ? TINT : INV, lineColor: i === 2 ? PRIMARY : LINE });
  if (i < 4) harrow(s, x + 2.2, x + 2.44, 5.6);
});
vline(s, 0.68 + 1.1, 6.07, 6.38, { arrow: false, color: PRIMARY_LT });
vline(s, 0.68 + 4 * 2.44 + 1.1, 6.07, 6.38, { arrow: false, color: PRIMARY_LT });
harrow(s, 0.68 + 4 * 2.44 + 1.1, 0.68 + 1.1, 6.38, { color: PRIMARY_LT });
label(s, 4.2, 6.42, 5.2, "…and the next iteration can use the new tool", { fs: 12, color: MUTED, align: "center" });
pageNo(s, 2);
s.addNotes("Frame the pain first (3 rows, left), then the idea (card, right): Lua on device + LLM on host = extensible instrumentation without reflash. The bottom loop is the structure of the whole talk — capture, analyze, generate, deploy, observe.");

// ============================================================
// S3 — SYSTEM AT A GLANCE
// ============================================================
s = p.addSlide();
s.background = { color: INV };
header(s, "SYSTEM · 10,000 FT", "System at a glance");
// host container
s.addShape(p.shapes.ROUNDED_RECTANGLE, { x: 0.5, y: 1.55, w: 5.5, h: 4.65, rectRadius: 0.06, fill: { color: PANEL }, line: { color: LINE, width: 0.75 } });
label(s, 0.75, 1.72, 3, "HOST PC", { fs: 13, bold: true, color: PRIMARY_DK });
const hostCards = [
  ["Cloud LLM — OpenAI-compatible", "Qwen (DashScope) active today · DeepSeek, OpenRouter, Ollama drop-in"],
  ["Host tools — Python, ~2.8k lines", "llm_loop.py · run_case.py · assistant.py, deliberately self-contained"],
  ["Test suites", "Unity C 126 · pytest 151 · one-command hardware gate run_all_hw.py"],
];
hostCards.forEach((c, i) => {
  s.addText([
    { text: c[0], options: { bold: true, fontSize: 13.5, color: TEXT, breakLine: true } },
    { text: c[1], options: { fontSize: 12, color: MUTED } },
  ], { x: 0.75, y: 2.12 + i * 1.32, w: 5.0, h: 1.18, shape: p.shapes.ROUNDED_RECTANGLE, rectRadius: 0.05, fill: { color: INV }, line: { color: LINE, width: 0.75 }, fontFace: F, margin: 8, paraSpaceAfter: 3, valign: "middle" });
});
// usb link
label(s, 6.0, 2.6, 1.55, "text commands", { mono: true, fs: 12, color: MUTED, align: "center" });
harrow(s, 6.05, 7.5, 2.98, { color: PRIMARY, w: 2 });
node(s, 6.0, 3.42, 1.55, 0.5, "USB CDC", { fill: PRIMARY, lineColor: PRIMARY, color: INV, bold: true, fs: 13.5 });
harrow(s, 6.05, 7.5, 4.36, { color: PRIMARY, w: 2, begin: true });
label(s, 5.85, 4.46, 1.85, "JSON lines ≤512B\ntagged with cmd", { mono: true, fs: 12, color: MUTED, align: "center", h: 0.55 });
// dongle container
s.addShape(p.shapes.ROUNDED_RECTANGLE, { x: 7.6, y: 1.55, w: 3.15, h: 4.65, rectRadius: 0.06, fill: { color: INV }, line: { color: PRIMARY, width: 1.25 } });
label(s, 7.8, 1.7, 2.8, "ESP32-S3 DONGLE", { fs: 12.5, bold: true, color: PRIMARY_DK });
const dongleCards = ["NimBLE observer —\npassive, continuous scan", "Scan pipeline —\nparse → filter → hook → JSON", "Lua 5.4 sandbox —\n128 KB static pool", "CONN central (opt.) —\none GATT peer"];
dongleCards.forEach((t, i) => node(s, 7.8, 2.12 + i * 1.0, 2.75, 0.86, t, { fs: 11.5, fill: i === 2 ? TINT : PANEL, lineColor: i === 2 ? PRIMARY : LINE }));
// BLE column
label(s, 11.0, 1.62, 1.83, "BLE · 2.4 GHz", { mono: true, fs: 12, color: MUTED, align: "center" });
node(s, 11.0, 2.25, 1.83, 1.05, "adverts from\nany nearby device", { fs: 11.5 });
node(s, 11.0, 4.05, 1.83, 1.05, "one GATT peer —\nnotify or polled", { fs: 11.5 });
harrow(s, 10.75, 11.0, 2.78, { dash: true, color: PRIMARY_LT });
harrow(s, 10.75, 11.0, 4.58, { dash: true, color: PRIMARY_LT });
note(s, "The dongle never exposes BLE toward the host — host traffic rides USB only; the radio is the product. Every box on this slide is a later slide.");
pageNo(s, 3);
s.addNotes("The one map to keep: PC on the left (cloud LLM + three Python tools + tests), USB CDC in the middle (commands out, JSON lines back), dongle on the right with its two radio roles: passive observer and optional GATT central. Point forward: each container gets its own section.");

// ============================================================
// S4 — [01] MODULE MAP
// ============================================================
s = p.addSlide();
s.background = { color: INV };
header(s, "01 · DONGLE FIRMWARE", "Nine components, ~5.7k lines of authored C");
const mods = [
  ["USB console", "166 L", "USB-Serial/JTAG line I/O; the Ctrl+C interrupt path."],
  ["Storage", "174 L", "LittleFS — 8 KB script slot at /littlefs/script.lua."],
  ["BLE scan", "355 L", "NimBLE observer; FNV-1a dedup in a ~1 s window."],
  ["BLE conn", "1,110 L", "optional GATT central; subscribe with poll fallback."],
  ["AD parser", "217 L", "decodes AD structures: name, UUIDs, manufacturer data."],
  ["JSON encoder", "739 L", "one line per event, ≤512 B, all dynamic text escaped."],
  ["Filter engine", "246 L", "up to 16 C rules evaluated on the hot path."],
  ["Lua + pool", "962 L", "Lua 5.4 sandbox; 128 KB static pool allocator."],
];
mods.forEach((m, i) => {
  const x = 0.5 + (i % 4) * 3.13, y = 1.58 + Math.floor(i / 4) * 1.86;
  s.addShape(p.shapes.ROUNDED_RECTANGLE, { x, y, w: 2.94, h: 1.7, rectRadius: 0.06, fill: { color: INV }, line: { color: LINE, width: 0.75 }, shadow: sh() });
  s.addText([
    { text: m[0], options: { bold: true, fontSize: 14, color: PRIMARY_DK, breakLine: true } },
    { text: m[1] + " · firmware/components/" + m[0].toLowerCase().split(" ")[0], options: { fontSize: 12, fontFace: M, color: MUTED, breakLine: true } },
    { text: m[2], options: { fontSize: 12, color: TEXT } },
  ], { x: x + 0.16, y: y + 0.12, w: 2.62, h: 1.46, fontFace: F, margin: 0, paraSpaceAfter: 4, valign: "top" });
});
hline(s, 0.5, RX, 5.48);
s.addText([
  { text: "Also on the die:  ", options: { bold: true, fontSize: 13, color: TEXT } },
  { text: "CLI 978 L — command dispatch + IDLE / SCANNING / SCRIPT_RUNNING state guards · bridge 254 L — per-line sandbox scan on script upload · power 187 L — auto light sleep with PM locks.", options: { fontSize: 13, color: MUTED } },
], { x: 0.5, y: 5.6, w: 12.33, h: 0.62, fontFace: F, margin: 0, valign: "top" });
label(s, 0.5, 6.32, 12.33, "main.c init chain → usb → storage → cli → ble → [conn] → pipeline → lua → bridge → power", { mono: true, fs: 12.5, color: PRIMARY_DK });
note(s, "+ ~29k lines of vendored Lua 5.4 (unmodified upstream) — full firmware tree ≈35.8k lines. Modules talk only through interfaces/*_if.h headers (~1.2k lines).");
pageNo(s, 4);
s.addNotes("Nine components, one rule: modules communicate only via the interfaces/ headers — that contract layer is why parallel AI agents could extend this codebase without stepping on each other. Line counts are real (cloc-style from the repo).");

// ============================================================
// S5 — [01] DATA PATH
// ============================================================
s = p.addSlide();
s.background = { color: INV };
header(s, "01 · DONGLE FIRMWARE", "One packet's journey: radio to JSON line");
label(s, 0.5, 1.48, 7, "SCAN PLANE — EVERY ADVERT IN RANGE", { mono: true, fs: 12, color: ACCENT, cs: 1.5 });
const scanN = ["NimBLE observer\nalways scanning", "Dedup\nFNV-1a · ~1 s", "AD parse\nproto layer", "C filter\n≤16 rules", "Lua on_adv\nkeep / drop", "JSON encode\n≤512 B line", "USB TX\none mutex"];
scanN.forEach((t, i) => {
  const x = 0.5 + i * 1.805;
  node(s, x, 1.82, 1.5, 1.1, t, { fs: 11.5 });
  if (i < 6) harrow(s, x + 1.5, x + 1.805, 2.37);
});
label(s, 0.5, 3.32, 8, "CONN PLANE — ONE GATT PEER, OPT-IN (F2.4)", { mono: true, fs: 12, color: ACCENT, cs: 1.5 });
const connN = ["GATT connect\nby UUID or address\n6 s timeout · 5 retries", "subscribe\nnotify / indicate", "no CCCD?\n→ poll reads\n100–10 000 ms", "payloads tagged\nfrom_poll", "re-stream as\n{\"src\":\"conn\"} lines"];
connN.forEach((t, i) => {
  const x = 0.53 + i * 2.495;
  node(s, x, 3.66, 2.27, 1.12, t, { fs: 11.5 });
  if (i < 4) harrow(s, x + 2.27, x + 2.495, 4.22);
});
label(s, 0.5, 5.12, 4, "INVARIANTS", { mono: true, fs: 12, color: MUTED, cs: 1.5 });
const invs = ["zero-malloc hot path — static buffers end to end", "state guards −911:\nIDLE / SCANNING / SCRIPT_RUNNING", "Ctrl+C (0x03) breaks in anywhere — even mid-poll", "one TX mutex — output lines never interleave"];
invs.forEach((t, i) => node(s, 0.5 + i * 3.13, 5.46, 2.94, 0.9, t, { fs: 12, fill: PANEL, lineColor: LINE }));
note(s, "Scan plane runs in one pipeline task (prio 2); the CLI task (prio 5) stays responsive through all of it. The conn plane is a separate worker task (4 KB stack).");
pageNo(s, 5);
s.addNotes("Walk one advert left to right: radio → dedup → parse → filter → Lua hook → JSON → USB. Then the second plane: an optional GATT connection whose subscribe can degrade to polling — that fallback is what the first_order_poll demo exercises. Close with the invariants; they are why the thing never falls over.");

// ============================================================
// S6 — [02] TWO PLANES
// ============================================================
s = p.addSlide();
s.background = { color: INV };
header(s, "02 · BLUETOOTH & USB", "Two planes: BLE toward the world, USB toward the host");
function plane(x, chipTxt, chipColor, rows) {
  s.addShape(p.shapes.ROUNDED_RECTANGLE, { x, y: 1.6, w: 5.92, h: 4.78, rectRadius: 0.07, fill: { color: PANEL }, line: { color: LINE, width: 0.75 } });
  s.addText(chipTxt, { x: x + 0.3, y: 1.85, w: 5.3, h: 0.5, shape: p.shapes.ROUNDED_RECTANGLE, rectRadius: 0.05, fill: { color: chipColor }, line: { color: chipColor, width: 0 }, fontFace: F, fontSize: 13, bold: true, color: INV, align: "center", valign: "middle", margin: 4 });
  rows.forEach((r, i) => {
    const y = 2.55 + i * 0.98;
    if (i > 0) hline(s, x + 0.3, x + 5.62, y - 0.1);
    s.addText([
      { text: r[0], options: { bold: true, fontSize: 13.5, color: TEXT, breakLine: true } },
      { text: r[1], options: { fontSize: 12.5, color: MUTED } },
    ], { x: x + 0.3, y, w: 5.32, h: 0.86, fontFace: F, margin: 0, paraSpaceAfter: 3, valign: "top" });
  });
}
plane(0.5, "BLE · 2.4 GHz — toward the world", PRIMARY, [
  ["Passive observer", "continuous discovery on BLE_HS_FOREVER — sees every device without connecting."],
  ["Optional central (F2.4)", "opt-in via Kconfig; connects by advertised service UUID or direct address."],
  ["Subscribe, then poll", "notify / indicate first; if the CCCD write fails, GATT reads every 100–10 000 ms."],
  ["Deterministic teardown", "CONN STOP returning ok ≠ off — hosts wait for CONN STATUS state:\"off\"."],
]);
plane(6.91, "USB · CDC 115200 — toward the host", PRIMARY_DK, [
  ["Lines in", "plain text commands; CR, LF, or CRLF line endings all accepted."],
  ["Ctrl+C = interrupt", "0x03 is polled even inside the conn worker — no command can lock the CLI."],
  ["JSON lines out", "one object per line, ≤512 B, serialized through a single TX mutex."],
  ["Keep the port open", "closing COM12 toggles USB power and resets the chip (ESP_RST_USB, note N3)."],
]);
note(s, "Both planes share one CLI dispatcher and one error model — hosts react programmatically instead of parsing prose.");
pageNo(s, 6);
s.addNotes("Key correction this slide makes for most audiences: BLE is NOT the host link. The dongle's radio talks to other devices; the host link is USB CDC. Left = radio behaviors (passive + optional central), right = the USB contract details that host tools rely on.");

// ============================================================
// S7 — [02] WIRE CONTRACT
// ============================================================
s = p.addSlide();
s.background = { color: INV };
header(s, "02 · BLUETOOTH & USB", "The wire contract: text lines in, JSON lines out");
node(s, 0.7, 1.62, 2.0, 0.55, "HOST", { fill: INK, lineColor: INK, color: INV, bold: true, mono: true, fs: 13 });
node(s, 10.63, 1.62, 2.0, 0.55, "DONGLE", { fill: INK, lineColor: INK, color: INV, bold: true, mono: true, fs: 13 });
hline(s, 1.7, 1.7, 5.3, { dash: true });
hline(s, 11.63, 11.63, 5.3, { dash: true });
const msgs = [
  [2.62, 2.35, true, "SCAN START\\r"],
  [3.32, 3.05, false, "{\"cmd\":\"scan\",\"status\":\"ok\",\"state\":\"SCANNING\"}"],
  [4.12, 3.85, true, "SCRIPT LOAD  →  paste Lua lines  →  SCRIPT END"],
  [4.92, 4.65, false, "{\"cmd\":\"script\",\"status\":\"loaded\"}   ·   per-line sandbox scan → −612 on reject"],
];
msgs.forEach(m => {
  label(s, 1.9, m[1], 9.5, m[3], { mono: true, fs: 12, color: TEXT, align: "center" });
  if (m[2]) harrow(s, 1.7, 11.63, m[0], { color: PRIMARY, w: 2 });
  else harrow(s, 1.7, 11.63, m[0], { color: PRIMARY, w: 2, begin: true, noEnd: true });
});
label(s, 0.5, 5.62, 9, "ERROR MODEL — EVERY MODULE OWNS A 100-WIDE NEGATIVE RANGE", { mono: true, fs: 12, color: MUTED, cs: 1.5 });
const errs = ["ADV −1xx", "JSON −2xx", "FILTER −3xx", "BLE −4xx", "USB −5xx", "LUA −6xx", "FS −7xx", "PIPE −8xx", "CLI −9xx"];
errs.forEach((e, i) => node(s, 0.5 + i * 1.345, 5.98, 1.3, 0.55, e, { mono: true, fs: 12, fill: PANEL, lineColor: LINE, color: PRIMARY_DK }));
note(s, "Every response carries its cmd, so hosts match answers to questions even when lines arrive stale — 76 CLI responses are strict-JSON-validated by tests.");
pageNo(s, 7);
s.addNotes("This is the whole protocol on one slide: four exchanges cover scan control and script upload. The cmd field is the anti-stale-line trick — after a Ctrl+C interrupt, old responses can't be mistaken for new ones. Error ranges mean the host can branch on the module that failed.");

// ============================================================
// S8 — [03] TOOL PACK CONVENTION
// ============================================================
s = p.addSlide();
s.background = { color: INV };
header(s, "03 · LUA TOOL REGISTRY", "H6.1 · A tool pack is just a Lua file");
s.addShape(p.shapes.ROUNDED_RECTANGLE, { x: 0.5, y: 1.6, w: 6.55, h: 4.75, rectRadius: 0.08, fill: { color: INK }, shadow: sh() });
const codeLines = [
  ["-- host_app/tool_packs/demo.lua", true],
  ["-- private globals carry the pack prefix", true],
  ["demo_t0 = nil", false],
  ["", false],
  ["-- every tool: ONE table arg, returns a string", true],
  ["function mean(a)", false],
  ["  local s = 0", false],
  ["  for _, v in ipairs(a.numbers) do", false],
  ["    s = s + v", false],
  ["  end", false],
  ['  return string.format("%.3f",', false],
  ["      s / #a.numbers)", false],
  ["end", false],
  ["", false],
  ["-- manifest(): JSON, one piece per line", true],
  ["function manifest()", false],
  ['  return \'{"version":1,"name":"demo",\'', false],
  ["      .. '\"tools\":[{\"name\":\"mean\", …]}'", false],
  ["end", false],
];
s.addText(codeLines.map(l => ({ text: l[0] || " ", options: { breakLine: true, color: l[1] ? CMT : CODE } })), { x: 0.78, y: 1.85, w: 6.0, h: 4.3, fontFace: M, fontSize: 12, margin: 0, valign: "top", lineSpacingMultiple: 1.05 });
const packRows = [
  ["One statement per line, ≤240 B", "packs ride the existing LUA EXEC path unchanged — zero firmware changes in M1."],
  ["manifest() is the API surface", "name, doc, typed args, returns, mutating flag, example — fetched in 180 B chunks (8 KB budget)."],
  ["One table argument, always", "mean({numbers = {3, 5, 10}}) — the system prompt teaches the model this exact shape."],
  ["The registry is host-side", "/tools caches manifests authoritatively; refresh detects a device reset and clears stale entries."],
];
packRows.forEach((r, i) => {
  const y = 1.66 + i * 1.12;
  if (i > 0) hline(s, 7.35, RX, y - 0.13);
  s.addText([
    { text: r[0], options: { bold: true, fontSize: 14, color: TEXT, breakLine: true } },
    { text: r[1], options: { fontSize: 12.5, color: MUTED } },
  ], { x: 7.35, y, w: 5.48, h: 1.0, fontFace: F, margin: 0, paraSpaceAfter: 3, valign: "top" });
});
["3 demo tools", "tests 79 → 108", "0 firmware changes"].forEach((t, i) => node(s, 7.35 + i * 1.88, 6.28, 1.72, 0.55, t, { mono: true, fs: 12, fill: TINT, lineColor: TINT, color: PRIMARY_DK, bold: true }));
note(s, "Loads are RAM-only (LUA EXEC run mode) and confirm-gated — nothing persists to LittleFS until milestone M2 (design decision 14).");
pageNo(s, 8);
s.addNotes("The registry's cleverness is that it added a whole extension mechanism with zero firmware changes: a pack is a plain Lua file obeying two constraints (one statement per line ≤240 B; manifest() describing each tool). The demo pack ships mean, temp_convert, bench_reset.");

// ============================================================
// S9 — [03] REGISTRATION + CAP-3 LOOP
// ============================================================
s = p.addSlide();
s.background = { color: INV };
header(s, "03 · LUA TOOL REGISTRY", "From /tools to a cap-3 execution loop");
label(s, 0.5, 1.48, 8, "REGISTRATION — /tools refresh", { mono: true, fs: 12, color: ACCENT, cs: 1.5 });
const regN = [
  ["/tools refresh\nhost asks the device", 0],
  ["LUA EXEC return\nstring.sub(manifest(), i, j)\n180 B chunks", 0],
  ["host validation\nstrict JSON · no reserved\nor duplicate names", 0],
  ["collision check — duplicate\npacks & tool names rejected\nBEFORE any line is sent", 1],
  ["[y/N] confirm\nloaded to RAM only", 0],
];
regN.forEach((n, i) => {
  const x = 0.53 + i * 2.495;
  node(s, x, 1.8, 2.27, 1.18, n[0], { fs: 11.5, fill: n[1] ? ATINT : INV, lineColor: n[1] ? ACCENT : LINE });
  if (i < 4) harrow(s, x + 2.27, x + 2.495, 2.39);
});
label(s, 0.5, 3.32, 5.5, "GENERATE-AND-EXECUTE — INSIDE ONE USER TURN", { mono: true, fs: 12, color: ACCENT, cs: 1.5 });
const genN = ["user asks a question\nabout live traffic", "LLM composes a tool\nprogram from the registry", "host runs it line-by-line\non the dongle", "result: tool> 6.000\nfed back to the model"];
genN.forEach((t, i) => {
  const x = 0.53 + i * 2.99;
  node(s, x, 3.98, 2.65, 1.0, t, { fs: 12 });
  if (i < 3) harrow(s, x + 2.65, x + 2.99, 4.48);
});
// loop back: node4 top -> up -> left -> down into node2
vline(s, 0.53 + 3 * 2.99 + 1.325, 3.72, 3.98, { arrow: false, color: PRIMARY_LT });
harrow(s, 0.53 + 3 * 2.99 + 1.325, 0.53 + 1 * 2.99 + 1.325, 3.72, { color: PRIMARY_LT });
vline(s, 0.53 + 1 * 2.99 + 1.325, 3.72, 3.98, { color: PRIMARY_LT });
label(s, 5.6, 3.47, 4.3, "each result informs the next program", { fs: 12, color: MUTED, align: "center" });
s.addText("CAP 3 — at most three consecutive executions per turn, then one corrective round forces a plain answer. A device error (e.g. sandbox −612) becomes the model's next hint instead of a crash.", { x: 0.5, y: 5.5, w: 12.33, h: 0.78, shape: p.shapes.ROUNDED_RECTANGLE, rectRadius: 0.06, fill: { color: ATINT }, line: { color: ACCENT, width: 1.25 }, fontFace: F, fontSize: 13, color: TEXT, align: "center", valign: "middle", margin: 10 });
note(s, "Live-verified 2026-09-03 — first real-hardware runs caught a stray brace and a nan-convention bug (evidence logs in harness/02-knowledge/). Programs defining on_adv / transform / manifest still take the human deploy gate.");
pageNo(s, 9);
s.addNotes("Top row: how a pack registers — the host pulls the manifest in chunks, validates strictly, and rejects name collisions BEFORE sending anything to the device. Bottom: the agent loop the LLM actually runs — compose, execute, read result, iterate — capped at three consecutive executions per turn so the model can't runaway-loop; then it must answer in plain text.");

// ============================================================
// S10 — [04] HOST APP
// ============================================================
s = p.addSlide();
s.background = { color: INV };
header(s, "04 · HOST LLM APPLICATION", "Three self-contained Python tools");
const tools = [
  ["llm_loop.py", "H5.1 · 486 lines", [
    "capture / analyze / deploy / loop — one subcommand each",
    "caps prompts at 30 deduplicated lines to bound token cost",
    "--dry-run works fully offline, no key needed",
  ]],
  ["run_case.py", "H5.2 · 813 lines", [
    "scenario harness + a controlled GATT peer on the PC (WinRT)",
    "--estimate: the LLM's JSON answer is checked against physics",
    "first_order & first_order_poll cases with ground-truth verdicts",
  ]],
  ["assistant.py", "H5.3 + H6.1 · 1,544 lines", [
    "interactive REPL over the live dongle, both planes drained",
    "typed envelopes: answer · lua · clarify · error",
    "/scan /conn /deploy /tools — sessions tee'd to timestamped logs",
  ]],
];
tools.forEach((t, i) => {
  const x = 0.5 + i * 4.195;
  s.addShape(p.shapes.ROUNDED_RECTANGLE, { x, y: 1.6, w: 3.94, h: 3.32, rectRadius: 0.07, fill: { color: INV }, line: { color: LINE, width: 0.75 }, shadow: sh() });
  s.addText([
    { text: t[0], options: { fontFace: M, bold: true, fontSize: 15, color: PRIMARY, breakLine: true } },
    { text: t[1], options: { fontFace: M, fontSize: 12, color: MUTED, breakLine: true } },
  ], { x: x + 0.24, y: 1.78, w: 3.5, h: 0.66, margin: 0, fontFace: F, paraSpaceAfter: 2, valign: "top" });
  s.addText(t[2].map((b, j) => ({ text: b, options: { bullet: bu(), breakLine: j < 2 } })), { x: x + 0.24, y: 2.52, w: 3.5, h: 2.25, fontFace: F, fontSize: 12.5, color: TEXT, margin: 0, paraSpaceAfter: 8, valign: "top" });
});
label(s, 0.5, 5.18, 9, "DEPLOY SAFETY CHAIN — WHAT HAPPENS TO EVERY GENERATED SCRIPT", { mono: true, fs: 12, color: MUTED, cs: 1.5 });
const chain = ["LLM replies\n{type:\"lua\", code}", "host parses —\nstrict JSON · one retry", "human gate\ndeploy? [y/N]", "SCRIPT LOAD —\nper-line sandbox scan", "device gate\n−612 = reject"];
chain.forEach((t, i) => {
  const x = 0.53 + i * 2.495;
  node(s, x, 5.52, 2.27, 0.88, t, { fs: 11.5, fill: i === 4 ? ATINT : INV, lineColor: i === 4 ? ACCENT : LINE });
  if (i < 4) harrow(s, x + 2.27, x + 2.495, 5.96);
});
note(s, "The three tools deliberately share conventions but zero code — parallel AI sessions can build features on disjoint branches without collisions.");
pageNo(s, 10);
s.addNotes("llm_loop = batch pipeline; run_case = reproducible scenario harness with a simulated plant; assistant = the interactive product. Emphasize the safety chain at the bottom: nothing the LLM writes reaches the radio without strict parsing, a human y/N, a per-line sandbox scan, and the on-device -612 gate as the last word.");

// ============================================================
// S11 — [04] PROVIDER RESILIENCE
// ============================================================
s = p.addSlide();
s.background = { color: INV };
header(s, "04 · HOST LLM APPLICATION", "One client, any OpenAI-compatible provider");
s.addShape(p.shapes.ROUNDED_RECTANGLE, { x: 0.5, y: 1.6, w: 5.35, h: 4.78, rectRadius: 0.07, fill: { color: PANEL }, line: { color: LINE, width: 0.75 } });
label(s, 0.8, 1.8, 4.8, "CONFIG — .llm_env (gitignored)", { fs: 13.5, bold: true, color: PRIMARY_DK });
const cfgRows = [
  ["Provider-agnostic", "any /chat/completions endpoint: OpenAI, DashScope (Qwen — active today), DeepSeek, OpenRouter, local Ollama."],
  ["Env wins as a unit", "if the shell has any LLM_* variable, the whole config comes from env; otherwise the file — never mixed. Mixing = guaranteed 401 (observed)."],
  ["Thinking off by default", "QWEN_ENABLE_THINKING=false — reasoning models stall >180 s on script-generation prompts."],
];
cfgRows.forEach((r, i) => {
  const y = 2.35 + i * 1.32;
  if (i > 0) hline(s, 0.8, 5.55, y - 0.12);
  s.addText([
    { text: r[0], options: { bold: true, fontSize: 13.5, color: TEXT, breakLine: true } },
    { text: r[1], options: { fontSize: 12.5, color: MUTED } },
  ], { x: 0.8, y, w: 4.75, h: 1.2, fontFace: F, margin: 0, paraSpaceAfter: 3, valign: "top" });
});
node(s, 6.3, 1.6, 6.53, 0.6, "POST /chat/completions", { fill: PRIMARY, lineColor: PRIMARY, color: INV, bold: true, fs: 13.5 });
vline(s, 7.85, 2.2, 2.52, { color: PRIMARY_LT, w: 1.5 });
vline(s, 11.3, 2.2, 2.52, { color: PRIMARY_LT, w: 1.5 });
label(s, 6.3, 2.56, 3.05, "PATH A · AUTH", { mono: true, fs: 12, color: MUTED, align: "center" });
label(s, 9.78, 2.56, 3.05, "PATH B · WRONG DIALECT", { mono: true, fs: 12, color: MUTED, align: "center" });
const healA = [["401 — key rejected", ATINT, ACCENT], ["one announced retry on .llm_env credentials", INV, LINE], ["✓ 200 — session continues", TINT, TINT]];
const healB = [["404 — base ends /api/v1", ATINT, ACCENT], ["heal to /compatible-mode/v1, retry once", INV, LINE], ["✓ 200 — new base sticks for the session", TINT, TINT]];
[healA, healB].forEach((col, c) => {
  col.forEach((n, i) => {
    const y = 2.92 + i * 0.92;
    node(s, c === 0 ? 6.3 : 9.78, y, 3.05, 0.72, n[0], { fs: 12, fill: n[1], lineColor: n[2], lineW: n[2] === ACCENT ? 1.25 : 0.75 });
    if (i < 2) vline(s, (c === 0 ? 6.3 : 9.78) + 1.525, y + 0.72, y + 0.92, { color: PRIMARY_LT, w: 1.5 });
  });
});
s.addText("Read timeouts are caught and surfaced as an envelope type:error — the session survives every provider hiccup.", { x: 6.3, y: 5.72, w: 6.53, h: 0.62, shape: p.shapes.ROUNDED_RECTANGLE, rectRadius: 0.05, fill: { color: INV }, line: { color: LINE, width: 0.75 }, fontFace: F, fontSize: 12.5, color: TEXT, align: "center", valign: "middle", margin: 8 });
note(s, "All three host tools self-heal identically — the convention is copied, not shared (same isolation rule as slide 10).");
pageNo(s, 11);
s.addNotes("Boring but battle-tested: the provider layer survived four .llm_env rewrites. Two self-heals — auth fallback to the file, URL dialect fix for Aliyun's native endpoint — plus thinking-mode off. The env-wins-as-a-unit rule exists because mixing an ambient key with the file's base URL is a guaranteed 401.");

// ============================================================
// S12 — [04] CLOSED-LOOP DEMO
// ============================================================
s = p.addSlide();
s.background = { color: INV };
header(s, "04 · HOST LLM APPLICATION", "The loop closes on real hardware: first-order plant");
const demoN = ["PC simulates plant\ny′ = (K·u − y) / τ", "PC serves it over\nWinRT GATT", "dongle auto-connects\nby service UUID", "re-streams {\"src\":\"conn\"}\nJSONL — 60/60 lines", "LLM returns JSON\nestimate of τ", "host checks vs\nground truth"];
demoN.forEach((t, i) => {
  const x = 0.5 + i * 2.01;
  node(s, x, 1.75, 1.85, 1.05, t, { fs: 11.5 });
  if (i < 5) harrow(s, x + 1.85, x + 2.01, 2.28);
});
label(s, 0.55, 3.14, 9, "FIRST_ORDER_POLL — THE DONGLE MUST POLL; NOTIFICATIONS DISABLED", { mono: true, fs: 12, color: MUTED, cs: 1 });
s.addText("9.5", { x: 0.5, y: 3.42, w: 3.4, h: 1.85, fontFace: F, fontSize: 100, bold: true, color: ACCENT, margin: 0, align: "left", valign: "middle" });
s.addText([
  { text: "τ̂ — the LLM's estimate, from raw JSONL alone", options: { bold: true, fontSize: 16, color: TEXT, breakLine: true } },
  { text: "true τ = 10.0 · error −5 %", options: { fontSize: 14, color: MUTED, breakLine: true } },
  { text: "±10 % tolerance → PASS", options: { fontSize: 14, bold: true, color: PRIMARY } },
], { x: 4.05, y: 3.72, w: 3.55, h: 1.35, fontFace: F, margin: 0, paraSpaceAfter: 6, valign: "middle" });
["60 / 60 conn lines captured — zero drops", "ground truth: 63.2 %-of-K settling rule", "notify variant (first_order) passes the same gate"].forEach((t, i) => node(s, 7.9, 3.52 + i * 0.78, 4.93, 0.62, t, { fs: 12.5, fill: PANEL, lineColor: LINE }));
note(s, "Live run, Aug 2026 — capture JSONL and transcript archived under harness/02-knowledge/ (README: host cases).");
pageNo(s, 12);
s.addNotes("The money slide. A first-order plant (tau=10) is simulated on the PC and served over GATT; the dongle must poll because notifications are off. 60 of 60 lines captured, and the LLM's estimate of tau from raw samples: 9.5 against a true 10.0 — inside the 10% tolerance. Physics-checked, not vibes.");

// ============================================================
// S13 — [05] TEST HARNESS
// ============================================================
s = p.addSlide();
s.background = { color: INV };
header(s, "05 · HARNESS & WORKFLOW", "From C unit tests to a one-command hardware gate");
s.addChart(p.charts.BAR, [{
  name: "checks",
  labels: ["Unity C (host-built)", "assistant (pytest)", "HW · conn (GATT)", "HW · BLE+Lua", "run_case (pytest)", "HW · bridge", "HW · power", "HW · peer", "llm_loop (pytest)"],
  values: [126, 108, 65, 45, 33, 32, 14, 11, 10],
}], {
  x: 0.5, y: 1.6, w: 7.55, h: 4.85, barDir: "bar",
  chartColors: [ACCENT, PRIMARY, PRIMARY, PRIMARY, PRIMARY_LT, PRIMARY, PRIMARY, PRIMARY, PRIMARY_LT],
  varyColors: true,
  chartArea: { fill: { color: INV } },
  showLegend: false, showTitle: false,
  catAxisLabelColor: MUTED, catAxisLabelFontSize: 12, catAxisLabelFontFace: F, catAxisOrientation: "maxMin",
  valAxisLabelColor: MUTED, valAxisLabelFontSize: 12, valAxisLabelFontFace: F, valAxisMaxVal: 140,
  valGridLine: { color: "EBF0F3", size: 0.5 }, catGridLine: { style: "none" },
  showValue: true, dataLabelPosition: "outEnd", dataLabelColor: TEXT, dataLabelFontSize: 12, dataLabelFontFace: F,
});
label(s, 8.35, 1.5, 4.5, "THE HARDWARE GATE — ONE COMMAND", { mono: true, fs: 12, color: MUTED, cs: 1.5 });
const gate = [
  ["python tests/hw/run_all_hw.py", INV, LINE, true],
  ["assert firmware VERSION first — wrong build stops everything", INV, LINE, false],
  ["6 suites as subprocesses — transcripts teed to evidence/<ts>/", INV, LINE, false],
  ["after EACH suite: reset_reason must be 11 (USB) or None", INV, LINE, false],
  ["all green = the merge gate", TINT, PRIMARY, true],
];
gate.forEach((g, i) => {
  const y = 1.84 + i * 0.99;
  node(s, 8.35, y, 4.48, 0.78, g[0], { fs: 12, mono: g[3], bold: g[3], fill: g[1], lineColor: g[2], lineW: g[2] === PRIMARY ? 1.5 : 0.75 });
  if (i < 4) vline(s, 10.59, y + 0.78, y + 0.99, { color: PRIMARY_LT, w: 1.5 });
});
note(s, "Unity C 126 = 108 unit + 10 response-contract + 8 fuzz — the fuzzer's first run found 3 real encoder defects. Soaks (2 h adv, conn reconnect every 300 s) run outside the gate. Source: tests/README.md · BRANCH.zcode.md.");
pageNo(s, 13);
s.addNotes("Left: the whole check inventory as one chart — C tests host-built with gcc, pytest suites for each host tool, and five hardware suites driven over the real COM port. Right: the hardware gate. The subtle detail is the reset_reason check after EVERY suite — if the chip rebooted for any reason other than USB replug, the gate fails.");

// ============================================================
// S14 — [05] QUALITY EARNED
// ============================================================
s = p.addSlide();
s.background = { color: INV };
header(s, "05 · HARNESS & WORKFLOW", "Quality was earned the boring way");
const qRows = [
  ["Fuzz from day one", "deterministic LCG inputs through both JSON encoders, a strict-JSON oracle, and every small buffer size.", "3", "real encoder defects\nfound & fixed", ACCENT],
  ["Golden transcripts", "frozen real device noise — ANSI NimBLE logs, CLI echoes, boot lines — replayed as fixtures; tripped a real stale-line weakness.", "76", "CLI responses strict-JSON\nre-validated", PRIMARY],
  ["Soak", "2 h advertisement soak watching the Lua pool for fragmentation; conn soak tearing down and reconnecting every 300 s.", "2 h+", "adv soak + conn soak,\nno leaks, no drift", PRIMARY],
  ["The ledger", "every evaluation logged and re-run per stage: 30+6 initial findings → 28 all-stage → 7 at v1.0.0 → all resolved; sessions file evidence in harness/02-knowledge/.", "ALL", "EVAL FINDINGS\nRESOLVED", ACCENT],
];
qRows.forEach((r, i) => {
  const y = 1.7 + i * 1.28;
  if (i > 0) hline(s, 0.5, RX, y - 0.16);
  s.addText([
    { text: r[0], options: { bold: true, fontSize: 16, color: TEXT, breakLine: true } },
    { text: r[1], options: { fontSize: 13, color: MUTED } },
  ], { x: 0.5, y, w: 8.4, h: 1.05, fontFace: F, margin: 0, paraSpaceAfter: 4, valign: "top" });
  s.addText([
    { text: r[2], options: { bold: true, fontSize: 30, color: r[4], breakLine: true } },
    { text: r[3], options: { fontFace: M, fontSize: 12, color: MUTED } },
  ], { x: 9.2, y, w: 3.63, h: 1.05, fontFace: F, margin: 0, align: "right", paraSpaceAfter: 2, valign: "top" });
});
note(s, "Numbers from bug_check/README.md, tests/README.md and FEATURES.zcode.md.");
pageNo(s, 14);
s.addNotes("The theme: no quality claim without an artifact. The fuzzer paid for itself on its first run; golden transcripts keep real-world noise in CI; soaks watch for long-horizon decay; and the ledger means every bug class found by an evaluation round is provably closed before the next stage.");

// ============================================================
// S15 — [05] FEATURE WORKFLOW
// ============================================================
s = p.addSlide();
s.background = { color: INV };
header(s, "05 · HARNESS & WORKFLOW", "Feature workflow: proposals, branches, manifests, evidence");
// branch diagram
s.addShape(p.shapes.LINE, { x: 0.7, y: 4.15, w: 5.3, h: 0, line: { color: PRIMARY, width: 2.25, endArrowType: "triangle" } });
[0.95, 2.35, 3.75, 5.55].forEach(x => s.addShape(p.shapes.OVAL, { x: x - 0.07, y: 4.08, w: 0.14, h: 0.14, fill: { color: PRIMARY }, line: { color: PRIMARY, width: 0 } }));
s.addShape(p.shapes.LINE, { x: 2.35, y: 2.6, w: 0, h: 1.55, line: { color: MUTED, width: 1.5 } });
s.addShape(p.shapes.LINE, { x: 2.35, y: 2.6, w: 1.4, h: 0, line: { color: MUTED, width: 1.5 } });
s.addShape(p.shapes.LINE, { x: 3.75, y: 2.6, w: 0, h: 1.55, line: { color: MUTED, width: 1.5, endArrowType: "triangle" } });
[2.62, 2.95, 3.28].forEach(x => s.addShape(p.shapes.OVAL, { x: x - 0.05, y: 2.55, w: 0.1, h: 0.1, fill: { color: MUTED }, line: { color: MUTED, width: 0 } }));
label(s, 1.95, 2.12, 3.6, "feature branch + BRANCH.zcode.md", { mono: true, fs: 12, color: PRIMARY_DK, align: "center" });
label(s, 1.85, 4.35, 1.7, "squash merge", { mono: true, fs: 12, color: MUTED, align: "center" });
label(s, 3.95, 4.35, 2.4, "registered baseline", { mono: true, fs: 12, color: MUTED, align: "center" });
node(s, 5.1, 3.5, 0.9, 0.44, "v1.1.0", { mono: true, fs: 12, fill: ACCENT, lineColor: ACCENT, color: INV, bold: true });
s.addShape(p.shapes.LINE, { x: 5.55, y: 3.94, w: 0, h: 0.14, line: { color: ACCENT, width: 1 } });
label(s, 0.7, 4.85, 5.3, "one feature per branch · cut from a registered baseline · never commit to a baseline directly", { fs: 12, color: MUTED, h: 0.55 });
// steps
const wf = [
  ["Design-first proposal", "docs/feature-proposal-*.md with numbered decisions — 14 of them for the tool registry."],
  ["Cross-agent review", "a second model (deepseek) reviews the proposal and the code before anything merges."],
  ["Branch from a registered baseline", "BRANCH.zcode.md tracks ACs, verification plan, changelog — no manifest, no merge."],
  ["Implement with evidence", "tests plus live-verification logs filed under harness/02-knowledge/."],
  ["Squash merge, keep the branch", "manifest deleted in the squash, FEATURES.zcode.md index updated, branch kept as the revert path."],
];
wf.forEach((r, i) => {
  const y = 1.66 + i * 1.04;
  s.addText(String(i + 1), { x: 6.5, y, w: 0.5, h: 0.9, fontFace: M, fontSize: 20, bold: true, color: ACCENT, margin: 0 });
  s.addText([
    { text: r[0], options: { bold: true, fontSize: 13.5, color: TEXT, breakLine: true } },
    { text: r[1], options: { fontSize: 12.5, color: MUTED } },
  ], { x: 7.05, y, w: 5.78, h: 0.95, fontFace: F, margin: 0, paraSpaceAfter: 3, valign: "top" });
});
note(s, "Commits follow type(scope): summary + What / Why / How / Verification sections — enforced by a commit-msg git hook.");
pageNo(s, 15);
s.addNotes("Process as architecture: the branch diagram is the git model — baselines only receive squash merges, feature branches carry a manifest that doubles as the merge contract. The five steps on the right are the lifecycle every feature walks; H6.1 (the tool registry) is the first feature to run the full ritual end to end.");

// ============================================================
// S16 — [05] AI-ASSISTED DEVELOPMENT
// ============================================================
s = p.addSlide();
s.background = { color: INV };
header(s, "05 · HARNESS & WORKFLOW", "Built by AI agents — what actually worked");
const aiRows = [
  ["A context layer written for machines", "CLAUDE.md brief · harness/00-global-context specs · 02-knowledge process reports", "“Context engineering beats prompt engineering”", "Martin Fowler, 2026"],
  ["Narrow agents on disjoint branches", "the three host tools share conventions, zero shared code", "“Single-responsibility agents outperform do-everything ones”", "field reports, 2026"],
  ["Models reviewing models", "deepseek reviews proposals and implementations pre-merge", "“Adoption is easy — trust is the bottleneck; review everything”", "Graphite guide, 2026"],
  ["Evidence gates, not vibes", "the HW gate + evidence logs decide merges; sessions answer “were the docs sufficient?”", "“90 % use agents weekly — only 29 % trust the output”", "JetBrains · Uvik, 2026"],
];
aiRows.forEach((r, i) => {
  const y = 1.68 + i * 1.08;
  if (i > 0) hline(s, 0.5, RX, y - 0.14);
  s.addText([
    { text: r[0], options: { bold: true, fontSize: 14.5, color: TEXT, breakLine: true } },
    { text: r[1], options: { fontSize: 12, color: MUTED } },
  ], { x: 0.5, y, w: 6.6, h: 0.95, fontFace: F, margin: 0, paraSpaceAfter: 3, valign: "top" });
  harrow(s, 7.25, 7.6, y + 0.42, { color: PRIMARY_LT, w: 1.5 });
  s.addText([
    { text: r[2], options: { fontSize: 12.5, color: PRIMARY_DK, italic: true, breakLine: true } },
    { text: r[3], options: { fontFace: M, fontSize: 11.5, color: MUTED } },
  ], { x: 7.75, y, w: 5.08, h: 0.95, fontFace: F, margin: 0, paraSpaceAfter: 2, valign: "top" });
});
const stats16 = [["90 %", "use agents weekly — JetBrains 2026", PRIMARY], ["2.3×", "merge rate for heavy users — LinearB 2026", PRIMARY], ["29 %", "trust the output — Uvik 2026 · why we gate on evidence", ACCENT]];
stats16.forEach((t, i) => {
  s.addText([
    { text: t[0], options: { fontFace: M, bold: true, fontSize: 22, color: t[2], breakLine: true } },
    { text: t[1], options: { fontSize: 12, color: MUTED } },
  ], { x: 0.5 + i * 4.16, y: 6.05, w: 4.0, h: 0.85, shape: p.shapes.ROUNDED_RECTANGLE, rectRadius: 0.05, fill: { color: PANEL }, line: { color: LINE, width: 0.75 }, fontFace: F, margin: 8, paraSpaceAfter: 2, valign: "middle" });
});
note(s, "Links in slides_outline.zcode.md — martinfowler.com, agents.md, JetBrains / LinearB / Uvik 2026 research.");
pageNo(s, 16);
s.addNotes("This project is also a case study in AI-assisted development. Left column: what we actually did. Right column: what 2026 research says — the mapping is close, which is the point. The 29 % trust stat motivates the whole evidence-first workflow: never merge on vibes, merge on the hardware gate.");

// ============================================================
// S17 — ROADMAP + CLOSE (dark)
// ============================================================
s = p.addSlide();
s.background = { color: INK };
label(s, 0.62, 0.5, 10, "esp32_lua_llm · what's next", { mono: true, fs: 12.5, color: MUTED_INV });
s.addText("Where it's headed", { x: 0.58, y: 0.92, w: 12, h: 0.85, fontFace: F, fontSize: 38, bold: true, color: INV, margin: 0 });
const road = [
  ["M2", "Persistent packs", "tool packs live in LittleFS slots and auto-register on boot — survive power cycles."],
  ["M3", "hw.* bindings", "Lua tools reach GPIO, UART, energy profiling — the dongle becomes an actor, not just an observer."],
  ["M4", "Native function-calling", "the assistant drops envelope parsing for real tool calls; the registry exposes itself over GATT."],
];
road.forEach((r, i) => {
  const x = 0.62 + i * 4.1;
  s.addShape(p.shapes.ROUNDED_RECTANGLE, { x, y: 2.1, w: 3.9, h: 1.62, rectRadius: 0.07, fill: { color: INK2 }, line: { color: "2C4254", width: 0.75 } });
  s.addText([
    { text: r[0] + "  ·  " + r[1], options: { fontFace: M, bold: true, fontSize: 13.5, color: ACCENT, breakLine: true } },
    { text: r[2], options: { fontSize: 12.5, color: MUTED_INV } },
  ], { x: x + 0.22, y: 2.28, w: 3.46, h: 1.3, fontFace: F, margin: 0, paraSpaceAfter: 6, valign: "top" });
});
label(s, 0.62, 4.02, 12, "Also queued: CONN STOP settle semantics · full battery + coverage pass · push master to origin.", { fs: 13, color: MUTED_INV });
s.addText("The dongle captures. The model thinks.\nThe loop closes.", { x: 0.58, y: 4.55, w: 12.2, h: 1.3, fontFace: F, fontSize: 27, bold: true, color: ACCENT, margin: 0, lineSpacingMultiple: 1.1 });
s.addText("~5.7k LOC authored firmware (35.8k with vendored Lua 5.4) · ~2.8k Python host tools · 126 Unity C tests\n108 + 33 + 10 pytest · 167 hardware checks · τ̂ 9.5 vs τ 10.0 · v1.1.0", { x: 0.62, y: 6.05, w: 12.1, h: 0.75, fontFace: M, fontSize: 13, color: MUTED_INV, margin: 0, lineSpacingMultiple: 1.25 });
label(s, 0.62, 6.98, 12, "E:\\agent\\esp32_lua_llm · Lua_tool_extension_dev → master (review pending)", { mono: true, fs: 12, color: MUTED_INV });
s.addNotes("Close on trajectory: M2 makes tools persistent, M3 turns the dongle into an actor, M4 makes the LLM integration native. Then the one-liner and the numbers strip. Q&A prompts: which device would you bridge first?");

// ---------- write ----------
p.writeFile({ fileName: "ESP32_Lua_LLM_Architecture.pptx" }).then(() => console.log("WROTE ESP32_Lua_LLM_Architecture.pptx"));
