#!/usr/bin/env python3
"""Chart-registry checker — keeps ASCII charts honest (see docs/chart-registry.zcode.md).

Every ASCII diagram in a *maintained* file carries a marker line next to it:

    <!-- chart-id: CH-readme-01 rev1 -->        (markdown / html)
    chart-id: CH-assist-py-01 rev1              (python docstrings)
    // chart-id: CH-cli-c-01 rev1               (c comments)

docs/chart-registry.zcode.md holds one tracking-table row per site:

    | CH-readme-01 | master | current | README.md:38-61 | ascii-plus | ... | 2026-09-26 | 1a2b3c4d | 1 |

This script recomputes each chart block's hash and cross-checks it against the
registry:

    MODIFIED  block hash differs from the registry row  -> paste the new chart
              into the catalog, bump rev + date + sha8 in the row
    ORPHAN    a chart-shaped block with no marker in a maintained path
              -> register it or exempt the path
    GONE      a registry row whose file no longer contains the marker
    STALE     row status says stale — refresh reminder

Advisory by design: run it before merging anything that touches docs.
Exempt (frozen) paths are never scanned for orphans: docs/archive/,
status/archive/, docs/templates/, docs/tostudy.md and the feature-proposal
docs are point-in-time records.

Usage:
  python scripts/check_charts.py                # cross-check vs registry
  python scripts/check_charts.py --scan         # dump detected blocks + hashes
  python scripts/check_charts.py --scan --all   # include frozen/exempt paths
"""
import argparse
import hashlib
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
REGISTRY = os.path.join(ROOT, "docs", "chart-registry.zcode.md")

# --- chart-block detection -------------------------------------------------
# STRONG lines are unambiguous chart syntax; WEAK lines (box interiors, rule
# lines) only extend a run that already contains a strong line — this keeps
# code lines like `+ list(...)` or `| {"json": ...}` from becoming charts.
STRONG_LINE = re.compile(
    r"[\u2500-\u257F]"            # unicode box drawing / arrows ──▶
    r"|-->|──>"                   # ascii flow arrows
    r"|\+[-]{3,}"                 # +----+ box edges
)
WEAK_LINE = re.compile(
    r"|^\s*[+|]"                  # box / table content lines
    r"|^\s*[+=-]{6,}\s*$"         # rule lines
    r"|^\s+[v^]{1,3}\s*$"         # lone flow arrowheads
)
MIN_BLOCK_LINES = 3
FENCE = re.compile(r"^\s*(```|~~~)")
MARKER = re.compile(
    r"chart-id:\s*(CH-[A-Za-z0-9]+(?:-[A-Za-z0-9]+)*-\d+)(?:\s+rev(\d+))?")
MARKER_WINDOW = 4          # marker must precede the block within N lines
REGISTRY_ROW = re.compile(
    r"^\|\s*(CH-[A-Za-z0-9]+(?:-[A-Za-z0-9]+)*-\d+)\s*\|")

TEXT_EXT = {".md", ".py", ".c", ".h", ".txt", ".html"}
# frozen point-in-time records: scanned only with --all, never ORPHANs
EXEMPT_PARTS = (
    os.path.join("docs", "archive") + os.sep,
    os.path.join("status", "archive") + os.sep,
    os.path.join("docs", "templates") + os.sep,
    os.path.join("vendor_reference") + os.sep,                          # third-party
    os.path.join("firmware", "components", "lua", "include") + os.sep,  # vendored Lua
    os.path.join("firmware", "components", "lua", "src") + os.sep,      # vendored Lua
)
EXEMPT_FILES = {"docs" + os.sep + "tostudy.md",
                "docs" + os.sep + "chart-registry.zcode.md",  # the catalog itself
                os.path.join("scripts", "check_charts.py")}   # own examples
SKIP_DIRS = {".git", "node_modules", "build", "build_off", "managed_components",
             "__pycache__", ".pytest_cache", ".claude", ".qwen", ".zcode",
             ".vscode", ".aider.tags.cache.v4"}


def is_frozen(rel):
    rel = rel.replace("/", os.sep)
    return (rel.startswith(EXEMPT_PARTS) or rel in EXEMPT_FILES
            or os.path.basename(rel).startswith("feature-proposal-"))


def chart_lines(text_lines):
    """Yield (index, line) for lines that look like chart content."""
    for i, ln in enumerate(text_lines):
        if CHART_LINE.search(ln):
            yield i, ln


def find_blocks(text_lines, fenced_only=False, fenced=None):
    """Group consecutive chartish lines (gap <= 2 lines allowed) into blocks.

    fenced_only: .md files — only content inside ``` fences is chart-eligible
    (plain pipe rows in markdown are tables, not charts).
    """
    regions = []
    if fenced_only:
        infence, start = False, 0
        for i, ln in enumerate(text_lines):
            if FENCE.match(ln):
                if infence:
                    regions.append((start, i))
                    infence = False
                else:
                    infence, start = True, i + 1
        if infence:
            regions.append((start, len(text_lines)))
    else:
        regions = [(0, len(text_lines))]
    pat_s, pat_w = STRONG_LINE, WEAK_LINE
    blocks = []
    for lo, hi in regions:
        run, run_strong = [], False
        for i in range(lo, hi):
            if fenced_only:
                hit = bool(pat_s.search(text_lines[i]) or
                           pat_w.search(text_lines[i]))
                strong = bool(pat_s.search(text_lines[i]))
            else:
                # outside md fences: only unambiguous chart lines count —
                # code lines like `+ list(...)` must not extend runs
                hit = strong = bool(pat_s.search(text_lines[i]))
            if hit:
                if run and i - run[-1] > 3:   # gap > 2 lines closes the block
                    if len(run) >= MIN_BLOCK_LINES and run_strong:
                        blocks.append(run)
                    run, run_strong = [], False
                run.append(i)
                run_strong = run_strong or strong
        if len(run) >= MIN_BLOCK_LINES and run_strong:
            blocks.append(run)
    out = []
    for run in blocks:
        text = "\n".join(text_lines[run[0]:run[-1] + 1]).rstrip("\n")
        sha8 = hashlib.sha256(text.encode("utf-8")).hexdigest()[:8]
        out.append({"start": run[0] + 1, "end": run[-1] + 1,
                    "lines": run[-1] - run[0] + 1, "sha8": sha8, "text": text})
    return out


def marker_before(text_lines, block_start):
    """Nearest CH- marker within the window above the block, if any."""
    lo = max(0, block_start - 1 - MARKER_WINDOW)
    for i in range(block_start - 1, lo - 1, -1):
        m = MARKER.search(text_lines[i])
        if m:
            return m.group(1), m.group(2)
    return None, None


def tracked_files():
    """Git-tracked text files (falls back to a directory walk without git)."""
    try:
        import subprocess
        out = subprocess.run(["git", "ls-files"], cwd=ROOT, capture_output=True,
                             text=True, check=True).stdout.splitlines()
        return [f.replace("/", os.sep) for f in out
                if os.path.splitext(f)[1].lower() in TEXT_EXT
                and not any(part in SKIP_DIRS for part in f.split("/"))]
    except Exception:                       # no git -> walk (untracked leak ok)
        files = []
        for dirpath, dirnames, filenames in os.walk(ROOT):
            dirnames[:] = [d for d in dirnames if d not in SKIP_DIRS]
            for fn in filenames:
                rel = os.path.relpath(os.path.join(dirpath, fn), ROOT)
                if os.path.splitext(fn)[1].lower() in TEXT_EXT:
                    files.append(rel)
        return files


def scan(path_all=False):
    """Return [{rel, block, marker, rev, frozen}] for text files."""
    found = []
    for rel in tracked_files():
        frozen = is_frozen(rel)
        if frozen and not path_all:
            continue
        try:
            with open(os.path.join(ROOT, rel), encoding="utf-8",
                      errors="replace") as f:
                lines = f.read().splitlines()
        except OSError:
            continue
        blocks = find_blocks(lines, fenced_only=(rel.endswith(".md")))
        if not blocks:
            # no plain-text chart here, but presence-only markers
            # (e.g. rendered diagrams) still count as sites
            for i, ln in enumerate(lines):
                m = MARKER.search(ln)
                if m:
                    found.append({"rel": rel, "block": None, "marker": m.group(1),
                                  "rev": m.group(2), "frozen": frozen})
            continue
        used = set()
        for b in blocks:
            mid, rev = marker_before(lines, b["start"])
            if mid:
                used.add(b["start"])
            found.append({"rel": rel, "block": b, "marker": mid,
                          "rev": rev, "frozen": frozen})
        # a marker with no block after it (chart deleted?) — GONE case
        for i, ln in enumerate(lines):
            m = MARKER.search(ln)
            if m and not any(bs > i and bs - i <= MARKER_WINDOW
                             for bs in used):
                found.append({"rel": rel, "block": None, "marker": m.group(1),
                              "rev": m.group(2), "frozen": frozen})
    return found


def parse_registry():
    """Parse the tracking table: id -> row dict."""
    rows = {}
    if not os.path.exists(REGISTRY):
        return rows
    with open(REGISTRY, encoding="utf-8") as f:
        for ln in f:
            m = REGISTRY_ROW.match(ln)
            if not m:
                continue
            cells = [c.strip() for c in ln.strip().strip("|").split("|")]
            rows[m.group(1)] = cells          # keep raw columns
    return rows


def cell(row, n):
    return row[n] if n < len(row) else ""


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--scan", action="store_true", help="dump blocks, no check")
    ap.add_argument("--all", action="store_true",
                    help="include frozen/exempt paths (with --scan)")
    args = ap.parse_args()

    items = scan(path_all=args.all)
    if args.scan:
        for it in sorted(items, key=lambda x: (x["rel"], x["block"]["start"]
                                               if x["block"] else 0)):
            b = it["block"]
            tag = it["marker"] or ("(frozen)" if it["frozen"] else "(no marker)")
            if b:
                print(f"{it['rel']}:{b['start']}-{b['end']} sha8={b['sha8']} "
                      f"lines={b['lines']} {tag}")
            else:
                print(f"{it['rel']} GONE-marker {tag}")
        print(f"-- {len(items)} chart sites")
        return 0

    registry = parse_registry()
    findings, ok = [], 0
    seen_ids = set()
    for it in sorted(items, key=lambda x: (x["rel"], x["block"]["start"]
                                           if x["block"] else 0)):
        b, mid = it["block"], it["marker"]
        if b and not mid and not it["frozen"]:
            findings.append(f"ORPHAN   {it['rel']}:{b['start']}-{b['end']} "
                            f"chart block has no chart-id marker "
                            f"({b['lines']} lines)")
        if not mid:
            continue
        seen_ids.add(mid)
        row = registry.get(mid)
        if row is None:
            findings.append(f"NO-ROW   {mid} has a marker in {it['rel']} "
                            f"but no tracking-table row")
            continue
        if b is None:
            if cell(row, 7):
                findings.append(f"GONE     {mid} marker present in {it['rel']} "
                                f"but no chart block follows it")
            else:
                ok += 1                # presence-only site (rendered diagram)
            continue
        status = cell(row, 2)
        if status == "stale":
            findings.append(f"STALE    {mid} ({it['rel']}) — refresh "
                            f"reminder, status=stale")
        sha8 = cell(row, 7)
        if sha8 and sha8 != b["sha8"]:
            findings.append(f"MODIFIED {mid} ({it['rel']}:"
                            f"{b['start']}-{b['end']}) chart changed since "
                            f"review — update catalog snapshot + sha8 "
                            f"({sha8} -> {b['sha8']}) and bump rev")
        else:
            ok += 1
    frozen_status = {"historical", "frozen-proposal", "template", "frozen",
                     "untracked"}   # not scannable until committed
    for rid, row in registry.items():
        if rid not in seen_ids and cell(row, 2) not in frozen_status:
            findings.append(f"MISSING  {rid} registry row but no marker found "
                            f"in the tree (deleted or not yet inserted)")
    print(f"chart check: {ok} verified, {len(findings)} findings "
          f"({len(registry)} registry rows)")
    for f in findings:
        print(" ", f)
    return 1 if findings else 0


if __name__ == "__main__":
    sys.exit(main())
