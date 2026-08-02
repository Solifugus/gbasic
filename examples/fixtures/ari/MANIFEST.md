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
| `teller_totals_generated.rpt` | the same, at scale and with wider variation | 2026-08-01, generated | 230 lines, 4 pages, form feeds. Reproduce exactly with `gbasic tools/gen_teller_report.bas 3 3 66 1 42`. |

## Two fixtures, two jobs

They are not redundant and neither replaces the other.

`teller_totals.rpt` is the **irregularity** fixture. Its value is that it is
inconsistent with itself in the ways a real report is, and those inconsistencies
were authored by hand because a template cannot invent them.

`teller_totals_generated.rpt` is the **scale and variation** fixture, produced by
`tools/gen_teller_report.bas`. A generator's natural output is regular, which is
exactly the wrong thing here, so that program deliberately generates the awkward
cases: pagination is a separate pass that counts lines and is blind to content,
so **page breaks land mid-section** — in the committed sample, one falls between
a `Bills / Coins` heading and its rows; money format varies **by branch**, so one
document contains several of the §5.1 forms; summary field order differs between
tellers; and column headings shift. It is deterministic (seeded RNG, fixed run
stamp, no clock), so regeneration is byte-identical and it can back a golden.

The committed sample is small on purpose. Larger corpora are generated on demand
rather than committed — 80 branches gives ~7 900 lines and 415 KB in 0.24 s.
When `tests/run_ari.sh` exists it should regenerate this file and diff it, so the
generator and the committed sample cannot drift apart.

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

## `delinquency.rpt` — the constructs teller_totals cannot reach

| file | shape modeled | authored | notes |
|---|---|---|---|
| `delinquency.rpt` | consolidated loan delinquency register | 2026-08-01, generated | 2 pages, form feeds, two tables per branch, wrapped rows. Reproduce with `gbasic tools/gen_delinquency_report.bas 2 2 60 1 7`. |

A second report *shape*: deeply hierarchical (region → branch → loans) around a
genuine table, where teller_totals is a flat sequence of blocks. It exists
because every value in teller_totals sits on the same line as its label, which
let four constructs inside ARI's own Phase 2 scope go unexercised. Each part
below forces one of them:

- **`OFFICER` on one line, the name on the next** → requires `down 1 of`. The
  entire vertical direction is untested without this.
- **`BRANCH TOTAL` prints its amount ABOVE the label**, under a rule line — the
  ordinary shape of a totals block → requires `up 2 of`.
- **The gap after `REMARKS:` varies by branch** — measured at 1, 2, 2 and 3
  lines in the committed sample → requires a distance RANGE. No exact distance
  matches all four, which is the honest reason ranges exist.
- **A `NOTES` line with a dotted leader to the right margin** → `flush`.
- **TWO tables per branch** (`CURRENT CYCLE` / `PRIOR CYCLE`), same column
  layout, each closed by a blank line or a rule → `repeats` combined with an
  explicit `ends`. One table per branch could not distinguish "found them all"
  from "found the first and stopped".
- **Every other loan row WRAPS** onto an indented `COLLATERAL:` line — one
  record, two physical lines → `rows continue(/pattern/)`. Rows that do not wrap
  leave `collateral` unknown, which is correct and surfaces as a diagnostic
  rather than a guess.
- **Dates are DD/MM/YYYY.** This is the residue §5.1 predicted: `03/04/2026` is
  3 April or 4 March and nothing in the token decides. It is the case the union
  recognizer genuinely cannot settle, and therefore the case `using date:` and
  custom `type` blocks exist for. Days 13–28 are used for most rows **on
  purpose**, so a wrong reading yields a plausible wrong date rather than an
  out-of-range error — guessing must not look safe.

Money here is uniform (plain, parenthesised negatives). Dialect mixing is
already covered by `teller_totals_generated.rpt`; repeating it would add size
without coverage.
