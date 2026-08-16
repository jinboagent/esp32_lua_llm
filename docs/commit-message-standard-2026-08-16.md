# Commit-Message Standard — Audit, Rewrite, and Enforcement (2026-08-16)

## Problem

Audit of all 47 commits found 46 non-compliant with the commit-message
standard (QWEN.md §1): 9 had no sections at all, 37 had an empty
`## What Changed` header (and no Why/How/Verification). Exactly one
commit (the H1/H2/H3 protocol fix) was fully compliant. Root cause:
nothing enforced the standard — no git hook was installed.

## What was done

1. **History rewrite (last 14 commits).** Every commit from the CR
   terminator fix through the zai session plan was replayed via
   cherry-pick with a reconstructed, standard-compliant message —
   What Changed bullets written from the actual diffs, Why/How/
   Verification sourced from the status/fix reports those commits
   carried. The already-compliant H1/H2/H3 commit was kept verbatim.
   File content is byte-identical to the pre-rewrite tree (verified by
   empty `git diff` against the backup branch). Old hashes are invalid;
   the pre-rewrite tip is preserved locally on branch
   `backup/pre-msg-rewrite-2026-08-16` (5f94dff). Force-pushed to
   origin/master with `--force-with-lease`.
2. **Enforcement hook.** `.githooks/commit-msg` rejects commits whose
   subject does not match `type(scope): summary`, that miss any of the
   four section headers, or whose What Changed section is empty.
   Merge/Revert/fixup subjects are exempt. Bypass:
   `git commit --no-verify`. Activated in this clone via
   `git config core.hooksPath .githooks` (per-clone step, documented
   in README).

## Verification

- All 15 rewritten-range messages pass the hook (validated
  programmatically); a deliberately bad message ("save stuff") is
  rejected with per-section diagnostics.
- `git diff backup/pre-msg-rewrite-2026-08-16 master` is empty — no
  file content changed by the rewrite.
- Older history (before the rewritten range) was left untouched by
  decision: the cost of a full-history force-push outweighed the
  benefit for 32 older commits; the hook prevents recurrence.

## Known quirk

Under MSYS `sh` on this machine, `HOME` resolves to `/home/jinbo`, so
git launched from shell scripts does not see the global
`safe.directory=*` and reports "dubious ownership". The hook itself
calls no git commands and is unaffected; helper scripts should run git
from cmd or set `HOME=%USERPROFILE%`.
