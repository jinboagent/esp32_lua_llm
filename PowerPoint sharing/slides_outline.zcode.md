# Slide outline — ESP32 Lua LLM architecture deck

**Author:** zcode · 2026-09-06
**Deck:** `ESP32_Lua_LLM_Architecture.pptx` (17 slides, 16:9) — build with `node build_deck.js`

Audience-level: high-level architecture walkthrough of the project as of v1.1.0 + branch `Lua_tool_extension_dev` (H6.1 M1).

| # | Section | Slide | Focal element |
|---|---------|-------|---------------|
| 1 | — | Title: "A BLE test lab on a USB dongle, operated by an LLM" | title + mini system schematic |
| 2 | — | Why this project exists | closed-loop diagram (capture → analyze → generate → deploy → observe) |
| 3 | — | System at a glance | full architecture diagram: HOST PC ⇄ USB CDC ⇄ dongle ⇄ BLE |
| 4 | [01] dongle | Firmware module map (9 components, ~5.7k lines) | card grid + init chain |
| 5 | [01] dongle | One packet's journey (scan plane + conn plane) | pipeline flow diagram |
| 6 | [02] link | Two planes: BLE radio side / USB host side | side-by-side panels |
| 7 | [02] link | Wire contract: text in, JSON out | sequence diagram + error-range chips |
| 8 | [03] registry | Tool pack convention (demo.lua) | code card |
| 9 | [03] registry | /tools registration + cap-3 generate-and-execute | two flow diagrams |
| 10 | [04] host | Three self-contained Python tools + deploy safety chain | 3 cards + chain |
| 11 | [04] host | Provider config + self-heal (401/404/thinking) | heal flow diagram |
| 12 | [04] host | Closed-loop demo: first-order plant, τ̂ 9.5 vs τ 10.0 | giant number |
| 13 | [05] harness | Test inventory bar chart + one-command HW gate | bar chart |
| 14 | [05] harness | How quality was earned (fuzz/golden/soak/ledger) | oversized stats |
| 15 | [05] workflow | Feature-branch workflow + governance steps | branch diagram |
| 16 | [05] workflow | AI-assisted development: practice ↔ research principle | mapping rows + 29% stat |
| 17 | — | Roadmap (M2–M4) + close | stat strip |

## Fact sources (all verified against the repo on 2026-09-06)

- Version v1.1.0 @ 71e0091; stages 0–5 complete; stage 6 on branch `Lua_tool_extension_dev` (BRANCH.zcode.md).
- Firmware ≈5,750 authored lines (35.8k incl. vendored Lua 5.4); host tools ≈2.8k Python; tests ≈4.6k lines.
- Tests: Unity C 126 (108 unit + 10 response-contract + 8 fuzz); pytest assistant 108/108 (79 → 108 on branch), run_case 33/33, llm_loop 10; HW suites conn 65, BLE+Lua 45, bridge 32, power 14, peer 11 (= 167 checks); 76 CLI responses strict-JSON validated; fuzz found 3 encoder defects.
- Demo: first_order_poll — 60/60 conn lines, τ̂ 9.5 vs τ 10.0 (−5%, ±10% tol), 63.2 %-of-K ground truth.
- Tool registry: line ≤240 B, ONE table arg, manifest chunked at 180 B (8 KB budget), collision rejection host-side before send, TOOL_EXEC_CAP 3, RAM-only loads.
- Protocol: JSON lines ≤512 B, `cmd` field on every response, error ranges −1xx…−9xx, Ctrl+C interrupt, keep-COM-open (N3).
- Providers: OpenAI-compatible; active Qwen/DashScope via `.llm_env`; 401 → .llm_env fallback; 404 `/api/v1` → `/compatible-mode/v1`; `QWEN_ENABLE_THINKING=false`.

## Web research cited on slide 16

- Context engineering for coding agents — martinfowler.com/articles/exploring-gen-ai/context-engineering-coding-agents.html
- AGENTS.md open standard — agents.md
- JetBrains Developer Research 2026 — 90 % of developers use AI coding agents weekly — blog.jetbrains.com/research/2026/08/ai-coding-agent-adoption-2026/
- LinearB 2026 — heavy AI users at 2.3× their June-2025 merge rate — linearb.io/resources/ai-engineering-productivity-gap
- Uvik 2026 — 84 % use daily, only 29 % trust output — uvik.net/blog/ai-coding-assistant-statistics/
- Stack Overflow 2026 — guidelines for AI (and people) — stackoverflow.blog/2026/03/26/coding-guidelines-for-ai-agents-and-people-too/

## Diagram best practices applied (from research)

- One diagram type per question: system context (S3), pipeline/data-flow (S5, S9, S12), sequence for time-ordering (S7), comparison panels (S6), branching model (S15) — per vFunction/Catio/Medium architecture-diagram guides.
- Every arrow labeled with the actual payload (commands / JSON lines / UUID connect) so diagrams double as protocol docs.
- Numbers pulled from the repo, each with a small muted source line — no invented figures.
- Built natively as PowerPoint shapes (not screenshots), so every box stays editable.

## What you (the presenter) should still do

1. Open the deck once in PowerPoint and skim for wording you'd say differently — all text is editable, nothing is an image.
2. Add your name/date on the title slide if you want attribution beyond the repo path.
3. Optional: replace slide 12's number block with a screenshot of the real run (evidence JSONL) if you have one you like.
4. Rehearse the 5 sections; speaker notes are attached to every slide (View → Notes).
