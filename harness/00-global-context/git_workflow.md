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

All branches are cut from `main`. No long-lived feature branches.

## Commit Messages

Format:

```
feat(stageN): short description

[optional body]
```

Examples:
```
feat(stage1): add USB CDC console with basic CLI parsing
feat(stage2): implement JSON encoder for ADV reports
feat(stage3): add Lua 5.4 sandbox with script load from LittleFS
fix(stage2): handle truncated AD structures without crash
refactor(stage1): extract proto_if.h from ble_scan
```

Rules:
- Type prefix: `feat`, `fix`, `refactor`, `docs`, `test`, `chore`.
- Stage tag in parentheses: `stage1`, `stage2`, `stage3`, `stage4`.
- Subject line: imperative mood, lowercase, no period, max 72 chars.
- Body (optional): explain *why*, not *what*. Wrap at 80 chars.

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
- Delete feature branch after merge.
- Never force-push `main`.
- Never commit directly to `main`.
