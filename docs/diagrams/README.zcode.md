# docs/diagrams — System Architecture Diagrams

> Author: zcode · 2026-09-16

## Files

| File | Description |
|------|-------------|
| `system_architecture_simple.zcode.png` / `.svg` | **Simple** architecture diagram (four blocks: LLM cloud / PC Host with 3 sub-blocks / USB Dongle / BLE client; abstraction level aligned with `harness/00-global-context/project_overview.md`) |
| `system_architecture_simple_en.zcode.png` / `.svg` | **English-label variant of the simple diagram** — used by `article/index.html` (copied to `article/img/system-architecture.png`); same layout, public-safe labels (zcode, 2026-09-25) |
| `system_architecture.zcode.png` | Full-detail architecture diagram (3600×2250, four domains + two focus panels) |
| `system_architecture.zcode.svg` | Vector version of the same content (infinitely scalable, embeddable in docs) |
| `dongle_llm_loop_sequence.deepseek.html` | **Sequence diagram** of the dongle ↔ host ↔ LLM loop: data plane (continuous adv stream) vs control plane (LLM session), and the two paths out of the LLM envelope (`LUA EXEC` tool program with result feedback vs hook-artifact deploy through the human gate). Fully self-contained — mermaid v10 is inlined, renders offline in any browser (deepseek, 2026-09-21) |
| `build_simple_diagram.py` | Simple-version generator script (edit and re-run to regenerate) |
| `build_simple_diagram_en.py` | English-label variant generator (article/public use; geometry fixes for longer English labels included) |
| `host_llm_message_prompt_en.zcode.png` / `.svg` | **Host-side LLM message-Prompt loop** (English): dongle + user inputs → MESSAGE Prompt → context assembly → cloud LLM → typed envelopes (answer/clarify to user, lua through the gate, tool programs capped at 3) + safety chain footer (zcode, 2026-09-28; used as `article/img/host-llm-loop.png`) |
| `build_host_llm_diagram_en.py` | Generator for the host-LLM loop diagram (article/public use) |
| `build_system_diagram.py` | Detailed-version generator script (matplotlib; edit and re-run to regenerate) |

Regenerate: `python docs/diagrams/build_simple_diagram.py` (simple) or
`python docs/diagrams/build_system_diagram.py` (detailed).

## Diagram content (why these four domains + two focus panels)

Main diagram: four domains, data flow left to right, closed loop:

1. **LLM (cloud)** — Chat Completions (OpenAI-compatible); three kinds of
   output (typed envelopes / tool_calls / estimation JSON) and four kinds
   of input.
2. **PC Host (Python)** — assistant.py (interactive + human-confirmed
   deploys), llm_loop.py (batch capture→analyze→deploy→verify),
   **run_case.py (simulation case: first-order plant + WinRT GATT server
   playing a real BLE peripheral)**, common layer (LLM client self-heal /
   tool-registry mirror / COM12 serial session).
3. **USB Dongle (ESP32-S3 firmware)** — control plane (usb+cli); adv data
   plane six-step pipeline (scan→proto→filter→Lua hooks→json_enc→output);
   conn data plane (GATT central, `{"src":"conn"}`, bypasses the Lua
   hooks); Lua runtime five-piece set (engine sandbox / hw.* /
   script_mgmt / pack_store / manifest); LittleFS storage; the
   "registration state lives in RAM, script text lives in flash"
   distinction.
4. **BLE environment** — any devices advertising (adv) and any GATT
   peripherals (conn), dual role on one board.

Purple dashed line = the user-requested "simulation loop": the PC
simulates a first-order plant → advertises over the air as a real BLE
peripheral via a WinRT GATT server → the dongle's conn plane re-streams
it → back to the PC over USB.

Two focus panels (bottom):

- **Focus A · Lua tool registration & invocation loop (H6.1)**:
  (1) load & register (convention-as-interface) → (2) chunked manifest
  fetch → (3) tools_array translation → (4a) native tool_calls /
  (4b) generate-and-execute; guardrail bar at the bottom (mutating gate /
  execution budget / sandbox).
- **Focus B · First-order system estimation simulation (run_case.py)**:
  plant formula `y += (dt/tau)*(K*u - y)` (dt-increment simulation) →
  GattPeer → dongle conn attachment → physics checks (63.2% time-constant
  point / steady state) → `--estimate` LLM estimate scored against ground
  truth; step-response inset chart at bottom right (63.2% checkpoint
  annotated).

Note: the two original generator scripts intentionally keep their diagram
label strings in Chinese (they render the PNG/SVG artifacts); per the
no-source-code-change rule only this README was translated. The `_en`
variant exists for public/article use, where the user asked for
all-English imagery (2026-09-25).

## Relationship to the existing architecture deck

The 17-slide deck under `PowerPoint sharing/` (2026-09-06) is
presentation-oriented; these diagrams target **engineering detail**: the
complete per-plane pipelines, the division of labor among the three host
tools, the simulation loop, and the Lua tool registration loop —
granularity the deck does not cover. The two complement each other.
