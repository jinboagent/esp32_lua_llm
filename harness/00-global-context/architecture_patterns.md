# Architecture Patterns & Feature-Patch Guide

| Field  | Value                                                       |
|--------|-------------------------------------------------------------|
| Scope  | Global — applies to every feature, every author (human or AI) |
| Status | Living design document; update when a pattern changes       |
| Works  | `docs/feature-proposal-ble-conn-2026-08-16.md` is the worked |
| example| example of this guide applied to a new feature              |

## 0. How to use this document

Read this + `coding_rules.md` + the single feature spec before writing
code. It answers three questions: *what architecture does this project
use* (§1), *why that architecture survives multiple AI authors* (§2),
and *how to patch a new feature in with minimal conflict and maximum
flexibility* (§3–§5).

## 1. The architecture, named

Layered **modular monolith** on ESP-IDF components, with a pipes-and-
filters data plane and state-machine control plane.

| Pattern | Where it lives in this repo | What it buys you |
|---|---|---|
| Modular monolith / component isolation | one component per concern under `firmware/components/` (`ble`, `cli`, `filter`, `lua`, `json_enc`, `power`, `usb`, `storage`, `proto`, `bridge`); explicit `REQUIRES` in each CMakeLists | a dependency graph you can see; no hidden coupling |
| Design by contract (ports) | `interfaces/*_if.h` — every module's public API, error ranges, buffer sizes, ownership rules | modules meet only through written contracts; internals stay private |
| Strict layering | `coding_rules.md` §5: L0 proto types → L1 drivers → L2 services → L3 pipeline → L4 CLI; no reverse dependencies | a new feature has exactly one correct shelf to sit on |
| Pipes & filters (data plane) | scan → parse → filter → Lua → encode → `usb_console_send_json` (`project_overview.md` §Data Flow) | new *sources* join by feeding the same pipe, not by forking it |
| State machines (control plane) | CLI 3-state (F4.1), bridge upload states (F4.2), conn states (F2.4) | behavior is enumerable → testable → no hand-waving |
| Composition root | `main/main.c` wires init order only, zero business logic | boot-order changes never touch logic |
| Pure core / impure shell | `json_enc`, `proto`, `filter`, `lua_pool` are host-testable pure C; IDF glue is thin | seconds-fast host verification, hardware only for radio proof |
| Namespace partitioning | error ranges per module (`coding_rules.md` §1), naming prefixes §4, buffer table §3 | parallel authors (human or AI) cannot collide on codes/names |
| Feature toggles | sdkconfig/Kconfig at build time; CLI state at runtime; Lua sandbox after deployment | flexibility without forks — Lua hooks are runtime-patchable product behavior |
| Fault isolation | every return code checked, boundary NULL/length validation, drop counters (`coding_rules.md` §9) | one module's failure can't corrupt another |
| Single stream contract | all machine data as JSON lines over USB CDC | host tools and the LLM loop see new data kinds for free |

## 2. Why this survives AI-generated code (the harness as coordination layer)

The harness is not just documentation — it is the **multi-author
coordination layer**:

- **Specs = task contracts.** One feature doc per AI session (never feed
  two features at once); AC/TC tables bound the author's freedom.
- **Test cases written before code.** TC rows in the spec are the
  acceptance gate; suites (host Unity, HIL) enforce them mechanically.
- **Every author leaves green.** 86+ host checks and the HIL suites must
  pass after each commit — regressions from any author surface at once.
- **Seams are the only meeting points** (§4). Everything else is
  file-private, so parallel work cannot conflict.
- **Audit & handoff.** 4-section commit messages (hook-enforced),
  `status/LATEST.md` pointers, the `02-future` parking lot, and
  review-first proposal docs for product-level changes — a fresh session
  or reviewer reconstructs intent without reading code.

## 3. Best-practice checklist for patching a new feature

1. **Spec first; review before code.** Park ideas in `02-future`; pull
   forward via a proposal doc when product-level (§8 of the conn
   proposal is the template).
2. **Classify against the seams.** Fits an existing seam → additive
   patch. Proves a seam is in the wrong place → redraw *that one
   boundary* and record the trigger (§5). Never pre-redesign.
3. **New files > appends at extension points > edits of old lines.**
   Example: `ble_conn.c` new; CLI gets one guarded branch; nothing else
   moves.
4. **Own a namespace, borrow a contract.** New module takes the next
   free error range, own CLI family, own `STATUS` sub-object, own test
   file, own Kconfig symbol — but speaks only through `proto_if` types,
   `usb_console_send_json`, `json_escape_str`; never includes another
   component's internals.
5. **Respect layering & memory rules.** Right shelf (§1 layering);
   static allocation, caller-owned buffers, boundary validation.
6. **Keep the pure core host-testable.** Radio-facing behavior gets HIL
   proof; everything else gets Unity tests that run in seconds.
7. **Flag it.** Build-time Kconfig + runtime toggle; the feature-**off**
   build behaving identically is the proof of zero impact.
8. **One feature per branch, per session.** Small commits, 4-section
   messages, suites green at every commit.
9. **Close the loop on sources of truth.** Promote spec to
   `01-features`, update `project_overview.md` diagrams/data flow,
   remove from `02-future`, update this guide if a pattern changed.

## 4. Extension points (the sanctioned meeting points)

These are the only places different authors are expected to touch
shared files; each is an *append*, never a rewrite:

- component `CMakeLists.txt` SRCS/REQUIRES lists (conditional append à
  la `LUA_SOURCE`)
- `interfaces/<module>_if.h` — append-only API sections + error ranges
- CLI dispatch chain + `s_is_cli_command` list in `cli_commands.c`
  (one guarded family per feature)
- `STATUS` response — nested sub-object per feature (additive JSON)
- `sdkconfig.defaults` / new `Kconfig.projbuild` symbols
- `tests/host/test_main.c` + CMake test registration
- root-level HIL suites (`test_*_hw.py`), one file per surface
- README command table / test-strategy table rows
- harness specs, `status/` reports, `docs/` proposals

## 5. Patch vs redesign rule

**Patch when the feature fits the seams; redesign when the feature
proves a seam is in the wrong place — and then redraw only that
boundary.** Record the refactor trigger in the spec instead of acting
on it preemptively.

Worked example: the Lua hooks are adv-shaped
(`on_adv(addr, addr_type, rssi, name, uuids, manu_id, manu_data)`).
Connection data bypasses them this version (fits nowhere cleanly); the
recorded trigger — "a second line source needs Lua processing" — is
when the pipeline gets generalized to source-agnostic line sources
(conn proposal §4, option C). Until then, patching wins.

## 6. Related documents

| Need | Where |
|------|-------|
| Product architecture & data flow diagrams | `project_overview.md` |
| Error ranges, naming, layering, concurrency | `coding_rules.md` |
| Commit & branch conventions | `git_workflow.md` |
| Feature spec template | `docs/templates/` |
| Parked v2+ ideas | `harness/02-future/README.md` |
| Worked example of this guide | `docs/feature-proposal-ble-conn-2026-08-16.md` |
