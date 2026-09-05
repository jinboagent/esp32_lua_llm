# Feature-Branch Workflow — baselines, branch manifests, feature index

> **Author:** zcode · 2026-09-03
> **Status:** ADOPTED — product-owner decision, 2026-09-03. Governs all
> feature development from this date. This *extends*
> `harness/00-global-context/git_workflow.md` — branch naming, commit
> format, squash policy and review checklists there are unchanged and
> still binding.

## 1. Why

One-feature-per-branch already isolates the work. What was missing is
traceability: landing on a branch (or on a baseline) you had to do git
archaeology to answer *what is this, where did it come from, what state
is it in?* This workflow turns those three answers into files you read:

- a **branch manifest** (`BRANCH.zcode.md`) on every feature branch —
  the branch's identity card
- a **feature index** (`FEATURES.zcode.md`) on every baseline — the
  list of features the baseline includes

## 2. Baselines

A **baseline** is a ref you cut feature branches from and merge them
back into. It must always sit in a releasable, verified state
(full suites green, or gaps known and recorded).

**Baseline registry** (this table is the registry — update it whenever
a baseline is added or retired):

| Baseline | Added | Notes |
|----------|-------|-------|
| `master` | project start | the only baseline today (2026-09-03) |

Adding a baseline later (e.g. a stabilization line cut for a firmware
release) is allowed: record it here first, and give it its own
`FEATURES.zcode.md` copied from master's.

## 3. Feature branches

- Naming unchanged: `feature/<stage-or-id>-<short-name>`
  (see git_workflow.md). `Lua_tool_extension_dev` is the already-cut
  seat for H6.1.
- The cut is recorded **in the manifest** at creation time: baseline
  name + commit sha + date. That anchor is what later answers "what did
  this branch actually start from" — including when the branch needs a
  rebase or a revert.
- One feature per branch; no long-lived branches; never commit
  directly to a baseline.
- Exception: the owner may explicitly instruct direct work on master
  (as happened for the 2026-08-29 test campaign). That is legitimate —
  but the work still gets a `FEATURES.zcode.md` entry marked *direct by
  owner instruction*, so the index stays complete.

## 4. The branch manifest — `BRANCH.zcode.md` (required)

- Lives at the repo root **on the feature branch**.
- Created at branch cut, **before any code commit**; updated as work
  progresses (status + changelog).
- No manifest → not ready to merge.

Template (copy verbatim, fill in):

```markdown
# BRANCH — <feature title>

> **Author:** <zcode or human> · <YYYY-MM-DD>

- **Branch:** feature/<n>-<slug>
- **Feature:** <one line — what this branch delivers>
- **Baseline:** master @ <short-sha> (<YYYY-MM-DD>) — commit this
  branch was cut from
- **Spec / proposal:** <path, e.g. docs/feature-proposal-*.zcode.md>
- **Status:** draft | in-progress | in-review | merged | abandoned

## Scope
In: …
Out (deliberately): …

## Acceptance criteria
- [ ] …

## Verification plan
unit suites / hw gate / live evidence — what proves this feature

## Changelog (append-only)
- <YYYY-MM-DD> <short-sha> one line

## Merge record (filled at merge time)
- squash commit: <short-sha> on master
- FEATURES.zcode.md entry added: <YYYY-MM-DD>
```

## 5. The baseline feature index — `FEATURES.zcode.md`

- Lives at the repo root **on each baseline** (master's exists today:
  `FEATURES.zcode.md`).
- One entry per included feature: what it adds, spec link, how it
  landed (branch + squash sha, or *direct by owner instruction*).
- Updated **as part of every squash merge**, not afterwards.

**What happens to the branch manifest at merge:** it is deleted in the
squash (a fixed filename would collide with the next branch's
manifest). Nothing is lost — the content survives in three places: the
`FEATURES.zcode.md` entry, the promoted spec in `harness/01-features/`,
and the kept feature branch's own history.

## 6. Merge ritual (checklist)

1. Manifest: status → `in-review`; acceptance criteria + verification
   plan filled in.
2. Squash-merge with the 4-section commit message (git_workflow.md).
3. Inside the merge: delete `BRANCH.zcode.md`; add/update the
   `FEATURES.zcode.md` entry (record branch name, cut sha, squash sha).
4. Keep the feature branch (revert path). Never force-push a baseline.
5. The review checklist in git_workflow.md still applies in full.

## 7. Agent pre-flight (any session that will write code)

- Am I on a feature branch? If not — cut one (unless the owner
  explicitly instructed otherwise).
- Does `BRANCH.zcode.md` exist and match reality (status, changelog)?
  If not — create or fix it **before** committing code.
- Before merging — run §6.

## 8. First application & retro-indexing

- **H6.1 Lua Tool Registry** → branch `Lua_tool_extension_dev`, cut at
  `e287261` (= master, 2026-09-03). Its `BRANCH.zcode.md` is created
  the day implementation starts (design-phase artifact:
  `docs/feature-proposal-lua-tool-registry-2026-08-29.zcode.md`).
- Stages 0–5 predate this rule; they are retro-indexed in
  `FEATURES.zcode.md` from `harness/01-features/` + git history, so the
  index is complete from day one.
