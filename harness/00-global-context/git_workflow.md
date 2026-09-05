# Git Workflow

## Branching

One feature per branch. Branch naming:

```
feature/<stage>-<short-name>
```

Examples:
```
feature/1-project-scaffold
feature/1-usb-cdc-console
feature/1-ble-scan
feature/1-ad-parser
feature/2-json-encoder
feature/2-filter-engine
feature/2-pipeline
feature/3-lua-engine
feature/3-lua-filter-integration
feature/3-script-storage
feature/4-cli-commands
feature/4-state-machine
feature/4-integration-test
```

All branches are cut from a registered **baseline** — today the only
baseline is `master` (this repo's baseline ref; older text here says
`main`, same meaning). No long-lived feature branches.

**Branch manifest (required since 2026-09-03):** every feature branch
carries `BRANCH.zcode.md` at the repo root — created at branch cut,
kept current — stating which feature the branch works on and which
baseline (+ commit sha) it was cut from. Every squash merge adds or
updates the feature's entry in the baseline's root `FEATURES.zcode.md`
index. Full policy + templates:
`docs/workflow-feature-branches-2026-09-03.zcode.md`.

## Commit Messages

Format:

```
type(scope): short summary

## What Changed
- list of specific file changes and what was modified

## Why (Decision/Rationale)
- what problem we solved
- why this approach was chosen over alternatives

## How (Process)
- steps taken during implementation
- any obstacles encountered and how they were resolved

## Verification
- how we confirmed the changes work (tests, flash, monitor output)
```

Examples:
```
feat(stage1): add USB CDC console with basic CLI parsing

## What Changed
- Added firmware/components/usb/usb_cdc_console.c
- Added interfaces/usb_if.h
- Added tests/host/test_usb_cdc.c (8 test cases)

## Why
The dongle needs a host communication channel for JSON output and
command input. USB CDC was chosen over UART because ESP32-S3 has
native USB — no external chip needed.

## How
1. Started with ESP-IDF TinyUSB CDC-ACM example
2. Added line buffering (accumulate until \n)
3. Added 256-byte RX buffer with overflow protection
4. Wrote host-side tests for buffer logic

## Verification
- Host tests: 8/8 pass
- Flashed to ESP32-S3, verified echo in terminal
```

```
fix(json): increase escaped_name buffer to prevent stack overflow (B1)

## What Changed
- firmware/components/json_enc/json_encoder.c: buffer 64→192 bytes

## Why
Control characters expand to 6 bytes each (\uXXXX). Worst case:
31 chars × 6 = 186 bytes > 64 byte buffer = stack overflow.
This is remotely triggerable via malicious BLE advertisement.

## How
Identified by automated bug checker (bug-luminous-spring.md B1).
Increased buffer to PROTO_DEVICE_NAME_MAX_LEN * 6 + 1.
Added return value check on s_encode_string_escaped().

## Verification
- Host tests: 11/11 pass (including escaping test)
- ESP32 build succeeds, binary size unchanged
```

Rules:
- Type prefix: `feat`, `fix`, `refactor`, `docs`, `test`, `chore`.
- Stage tag in parentheses: `stage1`, `stage2`, `stage3`, `stage4`.
- Subject line: imperative mood, lowercase, no period, max 72 chars.
- Body: ALWAYS include all 4 sections (What/Why/How/Verification).
- For bug fixes: reference the bug ID (e.g., B1, M2) from the bug report.

## Review Checklist (before merge to main)

### Correctness
- [ ] Code compiles with zero warnings (`-Wall -Wextra -Werror`)
- [ ] All return codes are checked by callers
- [ ] NULL checks at every module boundary
- [ ] Buffer length checks before every write
- [ ] No `malloc`/`free` anywhere in application code
- [ ] Static buffers sized per coding_rules.md table

### Architecture
- [ ] No reverse layer dependencies (upper calls lower only)
- [ ] Inter-module data uses `proto_adv_report_t` from `proto_if.h`
- [ ] Const pointers for read-only inter-module data
- [ ] BLE callback copies data and returns (no processing)
- [ ] Mutex protects shared resources (filter table, Lua state, LittleFS)

### Naming
- [ ] Static functions: `s_<module>_<action>`
- [ ] Public functions: `<module>_<action>`
- [ ] Structs: `<module>_<name>_t`
- [ ] Enums: `<module>_<name>_e`
- [ ] Macros: `MODULE_<NAME>`

### Testing
- [ ] Host unit tests pass (if applicable)
- [ ] Feature tested on hardware (ESP32-S3-DevKitC-1)
- [ ] JSON output verified against expected format
- [ ] Error paths exercised (queue full, NULL input, timeout)

### Documentation
- [ ] Feature requirement doc updated (harness/01-features/)
- [ ] Any new error codes added to coding_rules.md
- [ ] CLI commands documented (if applicable)

## Release Tags

Tag each stage completion on `main`:

```
v0.1.0  — Stage 1 complete (Foundation)
v0.2.0  — Stage 2 complete (Pipeline)
v0.3.0  — Stage 3 complete (Scripting)
v1.0.0  — Stage 4 complete (Polish) — v1 release
```

Tag format: `vMAJOR.MINOR.PATCH`
- MINOR increments per stage.
- PATCH for post-stage bug fixes.
- Tag message: `Stage N: <stage-name> complete`

Create annotated tags:
```bash
git tag -a v0.1.0 -m "Stage 1: Foundation complete"
git push origin v0.1.0
```

## Working Process Documentation

Every significant work session MUST produce a markdown report documenting what was done.

### When to Write a Process Report
- After fixing bugs (like `bug_check/bug_fix_report/fix-report-2026-07-19.md`)
- After implementing a feature
- After a design decision or architecture change
- After resolving a difficult debugging session

### What to Include
- **What was done**: specific changes made
- **Decisions made**: what options were considered and why one was chosen
- **Problems encountered**: obstacles and how they were resolved
- **Test results**: verification that changes work
- **Lessons learned**: anything surprising or worth remembering

### Where to Store
- Bug fixes: `bug_check/bug_fix_report/`
- Feature implementations: `harness/02-knowledge/`
- Design decisions: `harness/02-knowledge/`

### Rule
The process report is as important as the code. It captures the WHY that git commit messages summarize. Future AI sessions and human reviewers rely on these reports to understand context.

---

## Learning Roadmap Updates (tostudy.md)

After each work session, the agent MUST review what concepts the user encountered and update `tostudy.md`.

### Rules
1. **Append only** — NEVER delete existing content from tostudy.md
2. **Add new topics** when the user encounters a concept they didn't understand
3. **Add "What you just learned" sections** linking to the actual code/experience
4. **Track progress** — mark topics as the user studies them

### Format for New Entries
```markdown
## N. [Topic Name]

**Why:** [Why this matters for the project]

**Context:** [What happened in the session that triggered this]

### What to learn
- [specific concepts]

### Resources
- [links, docs, examples]

### Mini Project
[Hands-on exercise]
```

### When to Add
- User asked "what is X?" or "why do we need X?"
- User encountered an error they didn't understand
- A new technology/concept was introduced to the project
- The user expressed confusion about how something works

---

## Harness Methodology Reflection

After each stage completion or significant milestone, reflect on whether the harness engineering approach is working well and update the methodology.

### When to Reflect
- After completing each stage (0, 1, 2, 3, 4)
- After a major bug or integration issue
- When the workflow feels painful or inefficient
- When a better approach is discovered

### What to Reflect On
1. **Feature doc quality**: Were the feature docs sufficient for AI implementation? Too vague? Too detailed?
2. **Test strategy**: Did host-side tests catch real bugs? Were on-target tests sufficient?
3. **Build workflow**: Is the tmux-based build/flash/monitor approach efficient?
4. **Bug discovery**: Did automated tools find real issues? Were manual reviews effective?
5. **Commit traceability**: Can we trace any change back to its rationale?
6. **Knowledge capture**: Are lessons learned being recorded effectively?

### Where to Record Reflections
- `harness/02-knowledge/methodology-reflections.md` — append reflections with date
- Update this `git_workflow.md` if the methodology itself should change

### Format
```markdown
## Reflection — [Date] — [Stage/Event]

### What worked well
- ...

### What didn't work
- ...

### Methodology changes
- ...
```

---

## Workflow Summary

```
main ──────────────────────────────────────────────────────►
  \                                    \
   feature/1-project-scaffold           feature/3-lua-engine
   [commits]                            [commits]
   ↓ merge (squash)                     ↓ merge (squash)
main ──────────────────────────────────────────────────────►
                                        tag: v0.3.0
```

- Squash-merge to keep `main` history clean (one commit per feature).
- Every feature branch carries `BRANCH.zcode.md` (feature + baseline +
  cut commit); every merge updates the baseline's `FEATURES.zcode.md`
  index — policy: `docs/workflow-feature-branches-2026-09-03.zcode.md`.
- Dont need Delete feature branch after merge. keep the feature branch so we can revert if needed.
- Never force-push `main`.
- Never commit directly to `main`.
