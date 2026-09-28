# Publishing & Privacy Rules

> Author: zcode · 2026-09-28 · Established after the pre-publish secrets
> audit (result: clean). Read before anything from this repo leaves the
> machine or becomes public.

## The one rule

Only the orphan `release` branch ever goes public. Dev history (master,
feature branches) must never be pushed to a public repo, and the private
origin's visibility must never be flipped to public.

Why: dev history carries a personal author/committer email (a @163.com
address) and the machine's username appears in several dev-only docs and
evidence logs. The release commit was authored with the GitHub noreply
address and contains neither — keeping that separation is the whole
defense.

## Credentials

- `.llm_env` is gitignored and must never be committed (verified: never
  committed on any ref). Keys live at runtime only — shell environment or
  that file.
- `.llm_env.example` carries empty values only; keep it that way.
- Never paste real keys into docs, tests, fixtures, decks or articles —
  not even "example" keys formatted like real ones.

## Personal information

- Commit identity for anything public: the GitHub noreply address.
- No real email addresses in docs; use `user@example.com`.
- No absolute home paths (`C:\Users\<name>`, `/home/<name>`) in any doc
  that could ship; use repo-relative paths.
- No LAN/internal IP addresses.
- BLE addresses in docs and examples use the `AA:BB:CC:DD:EE:FF`
  placeholder only — never a real captured peer address.
- Evidence and HW-run logs (`harness/02-knowledge/`, `status/`) are
  dev-internal; never copy them into the release branch or a public
  article.

## Pre-publish audit (run before every public push)

Secret token formats (every command must return nothing):

```bat
git grep -I -n -E "LTAI[0-9A-Za-z]+|AKIA[0-9A-Z]{16}|ghp_[A-Za-z0-9]{20,}|AIza[0-9A-Za-z_-]{20,}|sk-[A-Za-z0-9]{16,}|BEGIN [A-Z ]*PRIVATE KEY" release
git grep -I -n -iE "(api[_-]?key|secret|token|password|passwd|credential)[\"' ]*[:=][\"' ]*[A-Za-z0-9+/]{8,}" release
```

Personal-info patterns (only documentation placeholders may match):

```bat
git grep -I -n -iE "jinbo|C:[/\\]Users|/Users/" release
git grep -I -n -E "\b(192\.168\.|10\.[0-9]+\.|172\.(1[6-9]|2[0-9]|3[01])\.)[0-9]+\.[0-9]+\b" release
git grep -I -n -E "\b([0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}\b" release
```

History checks on all refs (must return nothing):

```bat
git log --all --oneline -- .llm_env
git log --all -S"LTAI" --oneline
git log --all -S"BEGIN RSA PRIVATE KEY" --oneline
```

Baseline: full audit run 2026-09-28 on `release` (185 files, single
orphan commit) — clean on all of the above.
