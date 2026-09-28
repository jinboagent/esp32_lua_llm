# Documentation & Language Rules

> Author: zcode · 2026-09-28 · Working rules for docs, images and ASCII
> charts, established during the publishing / chart-governance work.

## Language

- All repo markdown, docs and code comments are written in English
  (user directive, 2026-09-16). Chat with the user may stay Chinese; the
  repo does not.
- Any image or diagram aimed at readers outside the repo — article,
  presentation deck, release docs — must use English labels. Diagram
  generators live in `docs/diagrams/`; English variants are the `*_en.py`
  scripts rendering the `*_en` PNG/SVG files. Chinese-label originals may
  stay for internal use, but the public asset is always the English
  variant.
- Markdown headed for Medium import: no tables (Medium's importer degrades
  them to flat lists) — use bold-lead bullet lists instead; keep one
  self-contained `index.html` with its figures in `article/img/` beside it
  (relative paths, committed together).

## New files

- Agent-authored markdown is author-tagged: `*.zcode.md` for ZCode,
  `*-deepseek.md` for DeepSeek, plus an author line under the title. Never
  rename or delete committed docs.

## ASCII chart governance

Every ASCII/Unicode box chart in tracked files is registered. Full policy
and the chart catalog live in `docs/chart-registry.zcode.md`; the
mechanical checker is `scripts/check_charts.py`.

- Each chart site carries a `chart-id: CH-<file-slug>-<NN>` marker next to
  the chart and one row in the registry's tracking table (location,
  master/alias role, status, sha8, rev, verified date).
- The registry catalog is the single reading copy: every distinct chart
  extracted, with a plain explanation, a how-to-read legend, and
  keep-in-sync instructions.
- Changed a chart → update the chart AND paste the new snapshot into its
  catalog section, bump the row's rev/date/sha8, then run:

  ```
  python scripts/check_charts.py    # must report 0 findings
  ```

- New chart → draw it, add the marker, add a row + catalog section, re-run
  the checker. Renamed file → update the row's Location column, keep the
  ID (the ID keeps its origin hint).
- Frozen copies (`docs/archive`, `status/archive`, `docs/templates`,
  `docs/tostudy.md`, `docs/feature-proposal-*`, `vendor_reference/`) are
  point-in-time records: cataloged, never maintained, exempt from the
  checker.
- Image/mermaid diagrams (PNG/SVG/HTML produced by the `docs/diagrams/`
  generators) are registered as presence-only rows — the checker verifies
  the site still exists, not a hash.
