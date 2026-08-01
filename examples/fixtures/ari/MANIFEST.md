# ARI fixture manifest

**Every file in this directory is SYNTHETIC.** Unlike
`examples/fixtures/edgar/MANIFEST.md`, which records real documents captured
from public URLs, nothing here was captured from anywhere. These are
hand-authored or generated page images that reproduce the *layout* of a class of
report. They contain **no real account, member, employee or institution data**,
and no proprietary content. Names, numbers, dates and identifiers are invented.

The distinction matters both ways: a reader who finds a file here that resembles
a production report must be able to tell at once that it is invented, and the
fixtures must never become a route by which real data enters the repository.

| file | shape modeled | authored | notes |
|---|---|---|---|
| `teller_totals.rpt` | credit-union teller totals report | 2026-08-01, hand-made | 76 lines, no form feeds — pagination is signalled by the header line alone (see §13.H). Deliberately irregular; see below. |

## `teller_totals.rpt` — the irregularities are the point

This file's value is not that it is tidy. Several properties look like flaws and
are the most useful things in it, because they are what defeats a
column-position parser and justifies anchor-relative identification:

- **Summary field order varies between tellers.** Teller 386 prints
  Beginning / Ending / Total; tellers 261 and 262 print Beginning / Total /
  Ending. A parser keyed to row offsets reads the wrong number for two of the
  three tellers and reports no error.
- **The `Amount` column heading shifts** — column 69 on the first detail table,
  column 72 on the others.
- **`Teller #:` in the summary block, `Teller#:` in the closing block** — the
  same field, spelled two ways in one document.
- **Negative amounts carry a TRAILING minus** (`$8,651.75-`), not a leading one.
- **`Bait Cash   $8,0000`** is malformed (four decimal zeros). Retained
  deliberately as a type-conversion failure case: per §8 it must degrade to
  `unknown` for that cell alone, never a silent zero and never a raise that
  sinks the import.
- **Branch context spans a page boundary** — `Branch: 14` appears only on the
  first page; the tellers on the second page belong to it implicitly.
- **Two structurally different section kinds** — a transaction table (columnar)
  and a Closing Cash denomination grid (two side-by-side label/value columns),
  which exercise different anchor directions.

- **`CHK#4211` glued into a free-text description column** — an identifier with
  no separator, in a field that is otherwise prose. Confirmed as a real pattern,
  not an artifact of hand-authoring.

Known accidental artifacts, NOT deliberate, to be corrected when the file is
next revised: the second page header repeats `Page 1` instead of incrementing,
and tellers 261 and 262 share the name `Ryan Bellbowl`.

## Divergences from the real report (confirmed 2026-08-01)

Recorded so the fixture's limits are not mistaken for the format's:

- **No form feeds.** The real report has them. Here the page break at line 27 is
  signalled by the header line alone. Useful by accident — it models the
  no-form-feed report style, which §13.H requires be parseable too — but it is
  not what the modeled report does.
- **Page breaks can occur ANYWHERE in the real report, including mid-section.**
  Pagination follows lines-per-page and is unrelated to logical structure, so a
  detail table can be cut in half. This fixture breaks tidily between tellers,
  which is the easy case.
- **One branch only.** The real report carries the full teller structure for
  every branch, so branch sections start at arbitrary points throughout and are
  themselves split across pages.

Not yet represented: a branch or grand-total rollup line, continuation/wrapped
lines, and pages beyond the second.

## Planned variation (see §5.1)

Fixtures here are deliberately NOT required to reproduce any one real report.
Reports of this class are internally inconsistent — different sections written
by different people across decades — so widening coverage by inventing sections
is explicitly in scope. In particular, money appears as `$1,234.56`,
`$    1,234.56` (right-justified in a fixed field), `1,234.56` (no symbol),
`$1,234.56-`, `-$1,234.56`, `$-1,234.56`, `<$1,234.56>`, `(1,234.56)` and
`1,234.56CR` — and more than one form can appear in a single document.
