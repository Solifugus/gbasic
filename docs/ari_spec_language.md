# ARI spec language — syntax draft

Status: **DRAFT, unexecuted.** `stdlib/ari.bas` does not exist, so nothing here
has been run. Every spec below was written and checked by hand against the two
committed fixtures. Treat the specs as claims to be verified by the
implementation, not as behavior.

Companion to `docs/text_design.md` (§4–§5 for the model, §5.1 for the variation
problem, §13.H for page furniture). This document tightens §5's sketch into
something implementable and revises it where the fixtures proved it wrong.

Drafted against:

- `examples/fixtures/ari/teller_totals.rpt` — hand-made, irregular, no form feeds
- `examples/fixtures/ari/teller_totals_generated.rpt` — generated, 4 pages, form
  feeds, money format varying by branch

## 1. What the fixtures forced

Four changes, each from a measurement rather than an opinion.

**1.1 §5's grammar was under-specified.** It gives
`field name: anchor [anchor ...]: pattern [, pattern ...]` with "multiple anchors
on one field are AND", but an anchor is separately defined as
`direction distance: pattern` — so the pattern list belongs to an anchor and to
the field at once, and the two readings disagree about where a `:` goes.
Rewritten in §3 below.

**1.2 A column heading cannot be used to locate its column.** The obvious
elegant locator — derive the value's column span from the heading above it — does
not survive contact:

| fixture | `Amount` heading ends at | value rows end at |
|---|---|---|
| `teller_totals.rpt` | col 74 | col 78, 78, **79** |
| `teller_totals_generated.rpt` | col 68 | col 79 |

The heading is misaligned with its own data by 4 and 11 columns. This is not a
fixture defect; headings and data drift apart over decades of separate edits.

**1.3 A fixed column span cannot locate the value either.** Look again at the
right-hand column above: rows in `teller_totals.rpt` end at 78, 78 and **79**.
The one that runs a column further is the negative amount, `$6,000.25-` — the
trailing minus is appended *after* right-justification, so negatives are one
column wider than positives in the same table. Any spec pinning that column to
78 truncates the sign; pinning it to 79 captures a leading space on every
positive.

So neither the heading nor the column can find the amount. What works is
**bounding by type**: "the last money-shaped token on this row." That drives the
central decision in §4 — `as <type>` does not merely convert a value, it
*delimits* it.

**1.4 Word-boundary anchoring is needed and is not available.** The closing-cash
grid contains both `Dollars` and `Half-Dollars`. Anchoring on the literal
`"Dollars"` matches inside `"Half-Dollars"`. The natural fix is `/\bDollars\b/`,
and TEXT-0 deliberately omitted `\b` (POSIX's `[[:<:]]`/`[[:>:]]` is not
portable — §13, original question 1). Ordering rescues this particular case
because the two words fall on different lines and the first match wins, but that
is luck, not design. Recorded as open in §8.

## 2. Page furniture (report-scoped)

Declared once, at the top of the spec, because furniture is stripped before any
section is located and there is therefore no section to attach it to (§13.H).

```
page:
    break: formfeed              ' or: break: /regex/
    drop: 2                      ' or: drop: through /regex/
```

- `break: formfeed` — furniture begins at a `\f`. The form feed may sit at the
  start of a line that also carries the header, which is what the generated
  fixture does.
- `break: /regex/` — furniture begins at any line matching. This is the
  no-form-feed style, which the hand-made fixture happens to model.
- `drop: <n>` — remove `n` lines counting from and including the break line.
- `drop: through /regex/` — remove from the break line through the first
  subsequent line matching, inclusive. For a header block of varying height.

Everything downstream sees the **clean grid**. `up`/`down` count over it, never
over the physical file, so an offset means the same thing regardless of where a
page happened to break. Diagnostics map back to physical line numbers.

## 3. Grammar

```
spec        := page-decl? type-decl* section+

page-decl   := "page:" INDENT ("break:" break-spec) ("drop:" drop-spec) DEDENT

type-decl   := "type" NAME ":" INDENT type-rule+ ("output:" base-type) DEDENT
type-rule   := pattern "->" transform* "as" base-type

section     := "section" NAME section-mod* ":" INDENT section-body DEDENT
section-mod := "repeats" | "starts(" pattern ")" | "ends(" pattern ")"
section-body:= using-decl* (field | section | rows)+

rows        := "rows:" INDENT field+ DEDENT
using-decl  := "using" base-type ":" NAME

field       := "field" NAME ":" locator ("as" type-ref)?
```

Three rules that the sketch left implicit and the fixtures made load-bearing:

- **A section's `starts(...)` line belongs to the section and is not offered to
  its repeating children.** Without this, a `rows:` block would take the column
  heading as its first data row.
- **`ends(...)` is optional.** A `repeats` section with no `ends` runs until the
  next occurrence of its own `starts` pattern, or the end of its parent —
  whichever comes first. That is what makes `Branch:` and `Teller:` work, since
  neither carries a terminator.
- **`rows:` means one record per remaining line** in the enclosing section, and
  makes that section's output a frame. It is the common case and deserves its
  own word rather than a `repeats` section with a synthetic line pattern.

## 4. Locators

A locator answers "where is the value", relative to the current record's lines.

| locator | meaning |
|---|---|
| `right of <pat>` | from just after the match to the end of the line |
| `left of <pat>` | from the start of the line to just before the match |
| `between <pat> and <pat>` | the span between two matches on one line |
| `columns <a>-<b>` | a fixed column span (0-based, inclusive) |
| `first <type>` / `last <type>` | the first/last token of that type on the line |
| `down <n> of <locator>` | apply the locator `n` lines below the anchor |
| `up <n> of <locator>` | the same, upward |
| `<locator> within columns <a>-<b>` | restrict the search to a column range |

Distances accept the §5 forms — exact (`5`), range (`2-10`, `3-`, `-8`) and
`flush`. A range means "search these offsets and take the first that matches",
which is what makes a spec survive a label-to-value gap that varies down a
column.

**`as <type>` delimits as well as converts** (§1.2, §1.3). `right of "Teller #:"`
on

```
Teller: Wendy Hermin         Teller #: 386                              Summary
```

yields `386                              Summary`; `as integer` reduces that to
`386` by taking the first integer-shaped token in the span. This is the locator
that survives both the drifting heading and the sign-widened column, and it
reads the way a person reads the report.

Extraction trims leading and trailing whitespace. It does **not** touch interior
whitespace, which matters for the §5.1 money form `$          6,045.75`, where
the gap between symbol and digits is padding inside a single value.

## 5. Types and scoped overrides

Built-in base types: `date`, `money`, `integer`, `decimal`, `text`. Each is a
**permissive recognizer** over the union of common forms, not a fixed pattern.

Because a real report contradicts itself between sections (§5.1), a scope may
rebind a built-in to a custom type:

```
using money: usd_bracketed
```

The binding applies to the declaring section and everything nested inside it,
and can be overridden further down. A field may also name a type directly,
which beats any `using` in scope:

```
field ending_cash: right of "Ending Cash" as usd_bracketed
```

Custom types are declared once at spec scope:

```
type usd_bracketed:
    /<\$?([\d,]+\.\d{2})>/   -> negate as decimal
    /\$?\s*([\d,]+\.\d{2})/  -> as decimal
    output: money
```

Rules are tried in order; the first that matches wins, so the negative form must
precede the general one. A value matching no rule becomes `unknown` for that cell
alone (§8 of the design) — never a silent zero, and never a raise that sinks the
import.

## 6. Worked spec — `teller_totals.rpt`

No form feeds; the header line is the page marker. One branch. Amounts use a
trailing minus.

```
page:
    break: /^[0-9]{2}\/[0-9]{2}\/[0-9]{4} .*Page [0-9]+$/
    drop: 2

type usd_trailing:
    /\$?\s*([\d,]+\.\d{2})-/   -> negate as decimal
    /\$?\s*([\d,]+\.\d{2})/    -> as decimal
    output: money

section report:
    using money: usd_trailing

    field branch: right of "Branch:" as integer

    section tellers repeats starts(/^Teller: /):
        field name:      between "Teller:" and "Teller #:"
        field teller_no: right of "Teller #:" as integer

        ' Anchored by LABEL, not by row offset. This is the case that matters:
        ' teller 386 prints Beginning/Ending/Total and tellers 261 and 262
        ' print Beginning/Total/Ending. A row-offset spec reads the wrong
        ' number for two of the three and reports nothing wrong.
        field beginning_cash: right of "Beginning Cash" as money
        field ending_cash:    right of "Ending Cash" as money
        field total_trans:    right of "Total Transactions" as money

        section detail starts(/^GL\s+Tran #/) ends(/^\s*$/):
            rows:
                field gl:      columns 0-24
                field tran_no: columns 25-37
                field tran_ty: columns 38-43 as integer
                field amount:  last money      ' NOT a column — see §1.3

    section closing repeats starts(/^Closing Cash in Drawer/):
        field teller_no: right of "Teller#:" as integer          ' note: no space
        field hundreds:  right of "Hundreds" as integer
        field fifties:   right of "Fifties" as integer
        field twenties:  right of "Twenties" as integer
        field dollars:   right of "Dollars" within columns 26-79 as integer
        field quarters:  right of "Quarters" within columns 26-79 as integer
        field bait_cash: right of "Bait Cash" as money
```

Two things to note. `Teller#:` in the closing block against `Teller #:` in the
summary block is the same field spelled two ways in one document, and the spec
simply says so — there is no cleverness available and none needed. And
`bait_cash` on the malformed `$8,0000` matches no rule of `usd_trailing`, so that
one cell becomes `unknown` while the rest of the record parses.

The `within columns 26-79` on `dollars` and `quarters` is the §1.4 workaround:
without `\b`, `"Dollars"` would otherwise be free to match inside
`"Half-Dollars"`.

## 7. Worked spec — `teller_totals_generated.rpt`

Form feeds; three branches; **money format varies by branch**, which is the case
`using` exists for. The structure is otherwise the same, so only the differences
are shown.

```
page:
    break: formfeed
    drop: 2

type usd_plain:
    /-\$?\s*([\d,]+\.\d{2})/   -> negate as decimal
    /\$?\s*([\d,]+\.\d{2})/    -> as decimal
    output: money

type usd_field:                          ' "$        -12,113.25"
    /\$\s*-\s*([\d,]+\.\d{2})/ -> negate as decimal
    /\$\s*([\d,]+\.\d{2})/     -> as decimal
    output: money

type usd_trailing:                       ' "12,573.25-"
    /([\d,]+\.\d{2})-/         -> negate as decimal
    /([\d,]+\.\d{2})/          -> as decimal
    output: money

section report:
    section branches repeats starts(/^Branch: /):
        field branch_no:    right of "Branch:" as integer
        field branch_total: right of /^Branch \d+ Total/ as money

        section tellers repeats starts(/^Teller: /):
            ' ... identical to §6 ...

    field grand_total: right of "GRAND TOTAL - ALL BRANCHES" as money
```

**This spec is incomplete on purpose, and the gap is the point.** `using money:`
is scoped lexically to a section in the spec, but the format here varies by the
*runtime* branch — branch 14 is `usd_plain`, branch 21 is `usd_field`, branch 28
is `usd_trailing`, and they are all instances of the **same** `section branches`
declaration. A lexical `using` cannot express "whichever dialect this instance
happens to use."

Three ways out, none chosen:

1. **Let the permissive default handle it.** Make built-in `money` recognize the
   full §5.1 union, and reserve `using`/custom types for cases the union cannot
   disambiguate. Simplest, and probably right for most reports — but it gives up
   on `1,234.56-`, which is genuinely ambiguous without knowing the dialect.
2. **Per-instance dialect detection** — a section declares a discriminator
   (`money detect: first-match-wins over a listed set`) resolved per instance.
   Powerful, and a new concept.
3. **Accept it.** Declare that a report mixing dialects across instances of one
   section needs those branches split into separate sections with distinct
   `starts` patterns. Honest, and unpleasant when there are forty branches.

My lean is **1 with 2 held in reserve**, on the grounds that the union recognizer
is needed anyway and the ambiguous forms are rarer than the merely various ones.
Resolving this is the last thing blocking implementation.

## 8. Open

- **§7's per-instance dialect problem** — the lean above needs confirming.
- **Word boundaries (§1.4).** ARI wants `\b`, TEXT-0 does not have it, and the
  `within columns` workaround only helps when a column split exists. Options:
  revisit `\b` in Layer 0 (a bounded translation to `[[:<:]]`/`[[:>:]]` where the
  libc supports it, refusing elsewhere), synthesize it in ARI by wrapping literal
  anchors in explicit delimiter classes, or leave it. This is the first concrete
  cost of that TEXT-0 decision and should be recorded against it either way.
- **Whether `columns` should exist at all.** §0 pitches ARI as anchor-relative
  *instead of* positional, yet §6 uses `columns 0-24` for three fields, because
  within one table the data columns genuinely are stable even when the heading is
  not. The honest position is that both belong, with anchors preferred; that
  should be stated in the design rather than left as an apparent contradiction.
- **Multi-line records.** Neither fixture has a wrapped or continuation line, and
  the grammar has no way to say "this record spans two physical lines." Real
  reports have them. Deferred, but it is a gap, not an omission.
