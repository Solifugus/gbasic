# gBASIC Excel (xlsx) module — design and implementation record

Status: **largely implemented.** This began as a proposal; decisions were taken
2026-08-01 and are recorded in §13, revising §3 (write path), §13.1–13.4 and the
§14 roadmap. Since then L0–L3 and both halves of L4 have shipped — read, write
and round-trip, the formula evaluator, workbook-wide dependency-ordered recalc,
grid extraction, consolidation, the SQLite loader, and phase 1 of the formula
compiler. **Read §13 before treating anything in §§1–12 as current:** the
sections above record the original reasoning, and where measurement overturned
it — which happened repeatedly, and is the most useful thing in this document —
§13 is what says so.

What remains of the roadmap: compiler phase 2 (`VLOOKUP`→join, `SUMIF`→filtered
aggregate — the refusals are in place and diagnostic, see §13.Q), streaming the
sheet parse, and making `grid.tables` lazy.

Scope is confirmed as **read + write + an internal formula engine**, staged.
This is not an import library: organizations in finance work *in* spreadsheets,
so the destination is round-trip fidelity plus the ability to evaluate what a
workbook contains.

A module for getting real work done with the spreadsheets that still run
FinTech, healthcare, banking, and back-office finance: read and write `.xlsx`,
pull clean tabular data out of messy real-world sheets, consolidate
heterogeneous sources into one shape, load sheets into database tables, and — the
ambitious payoff — run business-user Excel formulas *at scale* by compiling them
to set-based operations instead of interpreting them cell by cell.

This is a hard problem and **not 100% solvable in the general case** — there is no
reliable black box that "just understands" an arbitrary spreadsheet. The design's
posture is therefore **spec-driven with heuristic assists**: make the common
cases automatic, make the messy cases expressible and *testable*, and never
silently guess in a way that would corrupt a financial report.

Two pieces of existing gBASIC work make this far more tractable than it looks:

1. **An `.xlsx` file is a ZIP of XML.** The XML module (libxml2) already parses
   the content; only the ZIP container is new. Reading Excel is mostly
   orchestration over machinery we already have.
2. **The messy-extraction problem is ARI generalized to a grid.** The
   Anchor-Relative Identification notation designed for the text/pattern library
   (`docs/text_design.md`) applies almost unchanged to a 2D sheet — a spreadsheet
   is a *cleaner* substrate than free text because it is already a typed grid.

## 0. Motivation (why this is worth doing, and worth doing carefully)

Whole industries are driven by Excel. Business users fluently express logic as
formulas; analysts receive data as workbooks, not APIs. The recurring pain isn't
opening a file — it's that real workbooks are **irregular**: multiple tables per
sheet, totals and subtotals interleaved with data, headers that span two rows or
start on a different row than last quarter, and column names that drift between
sources. Consolidating such sheets (e.g. participation-loan tapes from many
credit unions for CECL reporting) today means bespoke Python per source.

There is also a scaling trap worth naming up front, because it shapes the whole
formula story. Excel Services and similar "run the workbook engine over big data"
approaches have repeatedly failed to scale — not because formulas are slow, but
because a **cell-by-cell calc graph** is the wrong execution model over millions
of rows. The value ("business users already know how to write formulas") is real;
the naive execution model is the mistake. This design keeps the value and
discards the model (§7).

## 1. Architecture — five layers

```
  L4  formula → set-op / SQL compiler   +   schema → DDL (sqlite / pg)
        │  "run the formula once over the whole dataset"
  L3  consolidation / mapping   (column aliasing, unit normalization, merge)
        │  many messy frames → one clean frame / table
  L2  region extraction  "ARI-for-grids"   → emits a frame
        │  find tables, skip totals, handle shifted / multi-row headers
  L1  grid model         a sheet as a 2D grid of typed cells
        │
  L0  xlsx I/O (C module)   read: unzip + XML module     write: emit xlsx
```

Each layer is independently useful and independently shippable. L0–L2 is the MVP
(read a workbook, get clean frames). L3 solves the consolidation problem. L4 is
the ambitious, optional payoff and comes last.

## 2. Decision A — target `.xlsx` (and `.csv`), not legacy `.xls`

- **`.xlsx`** (and `.xlsm` structurally) is Office Open XML: a ZIP of XML parts.
  It is the format that matters and the one the XML module can read.
- **Legacy `.xls`** is the binary BIFF format — a different, uglier parser with
  no XML leverage. Explicitly out of scope; the escape hatch is "re-save as
  `.xlsx`," which every Excel install offers.
- **`.csv`** is already served by `frame.read_csv`; the xlsx module defers to it
  rather than duplicating it, but L2/L3 (extraction, consolidation) accept CSV
  input too so the messy-header/consolidation tooling isn't Excel-only.

## 3. Decision B — reading and writing, and the dependency story

**Reading** an `.xlsx` = two capabilities:

1. **Unzip the container** — the one genuinely new native dependency. Options, in
   order of preference: `libzip` (clean API), `minizip` (ships with zlib),
   or zlib inflate over the ZIP directory by hand. Behind `HAVE_LIBZIP` (or
   equivalent) like every optional module.
2. **Parse the parts** — `xl/worksheets/sheetN.xml` (cells + references),
   `xl/sharedStrings.xml` (interned text), `xl/styles.xml` (number formats, which
   is how you tell a date from a serial number and money from a bare decimal),
   `xl/workbook.xml` (sheet names/order). This is the **existing XML module**.

**Writing** an `.xlsx` — two viable paths, a decision to settle in the doc-review:

- **`libxlsxwriter`** — a mature, single-purpose C library (behind
  `HAVE_XLSXWRITER`). Handles the zip, XML, styles, and number formats correctly;
  low risk. *Leaning this way* for correctness.
- **Self-emit** — write the XML parts and zip them ourselves (reusing the write
  side of the XML surface + the zip dep). No new dependency, more surface to get
  right (styles and shared strings are fiddly).

Per project convention, the module always builds; a missing native dep turns
`load xlsx` into a clean runtime error, not a build failure.

## 4. Decision C — the grid model (L1)

A worksheet is exposed as a **grid**: a record carrying the sheet name, used
range, and cells addressable by `(row, col)` (1-based, matching Excel) *and* by
A1 reference. Each cell is plain, inspectable data:

```
{ value: 1500.00, type: "number", fmt: "currency", ref: "C4", formula: "=B4*0.02" }
```

- **Cell types** come from the XML + style: `number`, `text`, `date`, `money`,
  `boolean`, `blank`, `error`. They map onto **native gBASIC values** (date and
  money already exist), so extraction yields real typed values, not strings.
- **Blank vs. missing**: an empty cell reads as `unknown` (gBASIC's NA), so
  sparse regions behave correctly downstream.
- **Formulas** are preserved as text on the cell (`formula`) alongside the cached
  value, which L4 needs.
- The grid is the substrate every higher layer reads; it is deliberately dumb
  (no table semantics) so the smart part lives in one place (L2).

Access surface: `xlsx.open(path)` → workbook; `xlsx.sheet(wb, name_or_index)` →
grid; `xlsx.cell(grid, "C4")`; `xlsx.range(grid, "A1:D20")` → a sub-grid;
`xlsx.sheets(wb)` → names. Writing: `xlsx.new()`, `xlsx.put(grid, "C4", value)`,
`xlsx.write_frame(grid, "A1", df)`, `xlsx.save(wb, path)`.

## 5. Decision D — region extraction is ARI-for-grids (L2), spec-driven

This is the layer that earns its keep and the one with no magic. It turns a grid
into one or more **frames**, coping with the irregularities real sheets have.

**Why ARI, generalized.** The text library's ARI locates a field by *what
surrounds it* rather than by fixed position. On a 2D grid that is even more
natural: a table's header row is "the row whose cells match these labels," a
data region is "everything below the header until a blank row or a totals row,"
a section is "the block starting where cell A-something says `Transactions`." The
same anchor concepts (`right`/`down`/`same`, distances, literal/regex/type
patterns, `starts`/`ends`, `break`) carry over; the substrate is just cells
instead of characters. Where practical, L2 **shares the ARI notation and engine**
with the text library so a user learns one spec language.

**What the spec expresses** (the problems you named, each with a handle):

| Real-world mess | How the spec handles it |
|---|---|
| Header on a different row than last time | Anchor the header by matching known labels, not row number |
| Two-row / merged headers | `header rows: 2` with a join rule (parent + child → one name) |
| Multiple tables per sheet | Multiple `section`s with `starts`/`ends` anchors |
| Totals / subtotals interleaved | `ends` on a totals anchor, or an explicit "drop rows where col A matches /total/i" filter |
| Trailing notes / blank gaps | `break on blank`; region ends at first blank row |

**Heuristic assist for the easy case.** When no spec is given, `xlsx.tables(grid)`
runs a best-effort detector (contiguous non-blank block, first row as header,
type-homogeneous columns) and returns candidate frames **plus a confidence note**
— never a silent commitment. The honest rule: **automatic when safe, spec when
not, and it tells you which.** For CECL-grade work the spec path is the supported
one, because a totals row silently absorbed as data is a reporting error, not a
cosmetic one.

**Output is a frame**, so L2 results flow straight into `frame`/`stats`/`chart`.

## 6. Decision E — consolidation / mapping (L3)

The participation-loan / CECL problem: many sheets, same *meaning*, different
*surface*. L3 is a declarative **mapping spec** applied to extracted frames:

- **Column aliasing** — many source names → one canonical name
  (`"Int Rate" | "Interest Rate" | "Rate (%)"` → `interest_rate`), including
  fuzzy/normalized matching (case, whitespace, punctuation) with an explicit
  alias table for the ones fuzz can't get.
- **Unit / representation normalization** — the "percentages expressed different
  ways" problem: `5`, `0.05`, `"5%"`, `"5.0 %"` → one canonical form via a
  per-column normalizer. Same for money (`$1,500.00` / `1500` / `(1,500)` for
  negatives), dates, and thousands separators. Reuses the text library's regex +
  type coercion.
- **Selection & required columns** — keep the common set; flag a source missing a
  required column rather than silently emitting `unknown`.
- **Merge** — stack normalized frames into one, tagging each row with its source
  so provenance survives (essential for audit/CECL).

```
canonical = xlsx.consolidate([frameA, frameB, frameC], mapping_spec)
```

The mapping spec is text, so it is versionable and **golden-testable** — the same
property that lets Adrian validate a consolidation by diffing the merged frame.

## 7. Decision F — mass formula execution: compile, don't interpret (L4)

The most valuable and most dangerous layer. The lesson from the field (Excel
Services failing to scale at a large health-plan services shop) is explicit in
this design: **do not port the cell-by-cell calc-graph engine.** That model is
what fails over large datasets.

**The reframe.** A business user's *column* formula is really a set operation.
`=IF(rate > 0.05, balance * 0.02, 0)` applied down a column is not a million cell
evaluations — it is one transformation over a column. So L4 is a **compiler**, not
an interpreter:

- **Parse** Excel formula syntax (references, ranges, operators, and a bounded
  function set: `IF`, `SUM`, `AVERAGE`, `MIN`/`MAX`, `ROUND`, `VLOOKUP`/`XLOOKUP`,
  `SUMIF(S)`, `COUNTIF(S)`, text and date functions, etc.).
- **Compile** to one of two targets:
  1. a **vectorized pass over a frame** (a `frame.with_column` transform) for
     in-memory datasets, or
  2. **SQL pushed down to `sqlite`/`pg`** for large datasets — `IF(...)` becomes a
     `CASE`, `SUMIF` becomes a filtered aggregate, a `VLOOKUP` becomes a join —
     so the database executes it once over the whole table.
- **Execute once**, set-based. This is the entire scaling story, and it is stated
  as the headline promise precisely because the naive alternative is known to
  fail.

**Honest bounds.** Not every workbook formula compiles cleanly — cross-sheet
volatile functions, circular references, and array-formula corner cases are out
of scope. L4 compiles the **column-formula subset business users actually write
for tabular data** and refuses (with a clear diagnostic) what it can't lower to a
set operation, rather than silently falling back to slow per-row evaluation. The
pitch is deliberately narrow and true: *"write your formula once; we run it
set-based, in-memory or in the database, over the whole dataset."*

**Schema → DB (the easy half of L4).** Independent of formulas: infer a table
schema from an extracted/consolidated frame (column names + inferred types),
generate `CREATE TABLE` / `ALTER TABLE`, and bulk-load via the existing
`sqlite`/`pg` modules. `xlsx.to_table(canonical, db, "loans")`. This ships before
the formula compiler and is valuable on its own.

## 8. Missing data, errors, and the no-silent-guess rule

- Blank cell ⇒ `unknown`; propagates per gBASIC NA policy.
- Field/column not found under a spec ⇒ `unknown`, never invented.
- Auto table detection ⇒ returns candidates **with confidence**, never a silent
  commitment; low confidence is surfaced, not hidden.
- Formula that can't be lowered to a set op ⇒ explicit diagnostic naming the
  formula, not a slow fallback.
- Corrupt/invalid xlsx or missing native dep ⇒ clean runtime error.
- The governing principle, especially for financial use: **automatic when safe,
  spec when not, and always say which.**

## 9. Determinism and testing

- **Reading** a fixed `.xlsx` fixture is deterministic → golden-file testable
  (`.xlsx` fixture + `.bas` that prints the extracted frame + `.out`).
- **Specs are text** (extraction + mapping) → versionable and golden-testable.
- **Formula compilation** is deterministic: a formula compiles to a fixed frame
  transform or a fixed SQL string, both diffable in a golden test *without*
  touching a database (assert the generated SQL), with a separate integration
  test that runs it against sqlite.
- Small binary `.xlsx` fixtures live under `examples/fixtures/` like the EDGAR
  offline fixtures. This is what lets Adrian own validation end to end.

## 10. Performance notes

- Read the shared-strings table and styles once; index them; resolve cells
  against the index (a cell references a shared-string id, not inline text).
- Build a sheet's grid as a list of rows once; iterate with `for each`, never
  indexed `while` (the O(n²) trap); assemble frame columns in one pass rather
  than `append`-ing cell by cell in a hot loop.
- For large data, L4 should prefer the **SQL push-down** path so row volume lives
  in the database engine, not in the interpreter — the whole point of §7.
- xlsx reading holds the sheet in memory; this targets business workbooks
  (thousands–low-millions of cells), stated as a bound. Truly huge tabular data
  belongs in the database via `to_table`, then queried, not held as a grid.

## 11. Non-goals (v1)

- Legacy `.xls` (BIFF) reading/writing — re-save as `.xlsx`.
- Charts, pivot tables, macros/VBA, conditional formatting round-trip — read
  data and formulas; don't reconstruct the presentation layer.
- A general Excel calc engine (volatile funcs, iterative/circular calc, full
  array formulas) — L4 compiles the tabular column-formula subset only.
- Streaming arbitrarily large workbooks — see §10; scale lives in the DB.
- Perfect automatic table detection — impossible in general; spec-first by design.

## 12. Integration

- **XML module** — the read engine for sheet/strings/styles parts.
- **text library / ARI** — L2 shares the anchor notation; L3 reuses regex + type
  coercion for normalization.
- **frame / stats / chart** — L2/L3 emit frames that flow into the whole
  analytics toolkit with no glue.
- **sqlite / pg** — L4's schema-to-DDL, bulk load, and formula push-down targets.
- **EDGAR / FinTech workflows** — consolidate loan tapes, financial exhibits, and
  analyst workbooks into queryable tables and charts.

## 13. Decisions (2026-08-01)

**A. Write path: a PART TREE, not `libxlsxwriter`. Reverses §13.1's original
lean.** The premise of that lean was that writing is a separate act from
reading. It is not, once round-trip is in scope: `libxlsxwriter` *generates new
workbooks and cannot edit an existing one*. So the moment "someone sends us a
workbook, we change three cells, we send it back" is a requirement, that library
is the wrong tool at any level of quality.

The shape instead: unzip the container into a **part tree**, retain **every**
part, and on write re-emit untouched parts **byte-for-byte**, regenerating only
what changed. This is the only approach that does not silently destroy the
charts, pivot tables, conditional formatting and macros we do not model. It also
needs no new dependency.

*Consequence for the reader, and it is the expensive one to get wrong:* the
reader may discard nothing. A cell needs its cached value, its formula text AND
its style/number-format reference; the workbook needs the shared-string table
and every part we do not understand. Deferring write until after extraction
would mean discovering late that the reader threw away what write needed —
which is why §14 now sequences write **before** extraction.

**B. Unzip: zlib plus our own ZIP container reader.** Measured on the
development box: `libzip` **absent**, `minizip` **absent**, `zlib` **1.3.1
present**. Choosing libzip means installing a dependency before anything can be
built or tested, here and again on the RISC-V target. zlib is effectively
universal and gives `inflate`/`deflate`; the container itself — central
directory, local headers, CRCs — is a few hundred lines of ours. The nicer API
is not worth the deployment cost.

**C. VBA: preserved, never executed.** Executing it is a VB6 interpreter and is
out of scope permanently. Preserving is nearly free under the part tree, since
`vbaProject.bin` is just another part copied through. Two details need a test
rather than an assumption: the package relationships referencing it must survive
regeneration, and the file must keep its `.xlsm` content type or Excel rejects
it.

**D. TWO formula engines, for two different jobs. Both wanted; recalc first.**
This replaces §13.4's either/or framing.

- **Local recalculation** — a dependency graph over a workbook's cells,
  evaluated in process, no database. For workbook fidelity: change an input, see
  dependents update; write a formula, emit a correct cached value.
- **Set-based compilation to SQL** — the L4 payoff, targeting Postgres *and*
  SQLite (a common subset, so local work needs no server). `IF`→`CASE`,
  `SUMIF`→filtered aggregate, `VLOOKUP`→join, run once over millions of rows.

§1's warning stands and is not in conflict: a cell-by-cell calc graph is the
wrong model for *bulk data*. It is exactly the right model for *a workbook*,
which is what Excel itself does.

*Why recalc is first, and the second reason is the strong one:*

1. **They share a front end.** Both need an Excel formula parser producing an
   AST; they differ only in the back end. The parser is the bulk of the work and
   the compiler inherits it, so this is not a detour.
2. **Recalc has a free, exact test oracle — and it then becomes the compiler's
   oracle.** An xlsx stores both the formula and *Excel's own cached result* for
   every formula cell, so an evaluator can be checked against Excel cell by cell
   on any real workbook. Nothing comparable exists for the compiler: SQL that
   runs successfully over a million rows and returns a subtly wrong number looks
   exactly like SQL that is right. With recalc validated first, the two engines
   can be run over the same workbook and *required to agree* — the same
   cross-check that caught the date bug in ARI, where a `/re/repl/` transform
   and a `-> dmy` dialect had to produce identical output.

**E. Data representation: the workbook is a HANDLE; values crossing to gBASIC
are plain data.**

The deciding factor is mutation, not speed. gBASIC arrays and records have value
semantics with copy-on-write, and **a function cannot mutate its caller's
state**. A gBASIC recalc engine would have to thread the whole grid through
every recursive call and return it. `stdlib/ari.bas` demonstrated the cost of a
much milder version: threading an append-only *diagnostics list* through nested
sections was the ugliest part of that library, and a mutable 50 000-cell grid
with a dependency graph over it would be far worse — where every bug is a
silently stale cell, the worst failure a spreadsheet can have.

Speed is secondary but real: gBASIC does roughly a million simple array
operations per second (measured, `tests/run_arridx.sh`), and a formula is a few
dozen interpreter steps in a gBASIC-written evaluator.

**The line this project already draws is the right one.** `sqlite`, `pg`, `xml`
and `process` are handles — mutable state with identity. `ari`, `persist`,
`frame` and `stats` are pure gBASIC — immutable input, value output. ARI is pure
gBASIC *because* its input is immutable text. A workbook being recalculated is
unambiguously the first kind. So:

- **C:** the workbook handle (sparse cell store — sheets are mostly empty), the
  ZIP/part tree, the formula parser, the dependency graph, the evaluator.
- **gBASIC:** the ARI-for-grids spec, extraction to frames, consolidation and
  mapping, driver logic — and the compiler's target, since frames and SQL are
  gBASIC-side.
- **The boundary:** a *cell* handed to gBASIC is a plain **record**
  (`{value, formula, type, ...}`), never a handle. Only the workbook is a
  handle. Everything stays inspectable and golden-testable; you simply cannot
  hold half a workbook by value.

This costs nothing on testing: recalc in C is still driven from gBASIC, so the
Excel-cached-value oracle works exactly the same.

**F. Shared ARI notation — confirmed, with what actually transfers now known.**
ARI shipped on 2026-08-01, so this is no longer speculative:

- *Transfers directly:* `starts`/`ends`, nesting, `repeats`, the union type
  recognizers (arguably **more** valuable on a sheet, where exported reports are
  full of numbers stored as text), and the diagnostics/`inspect` mechanism.
- *Transfers with a change of substrate, and gets simpler:* `columns a-b`
  becomes a cell range; `right of "LABEL"` becomes "the cell right of the cell
  containing LABEL". Cells are already discrete, so no character scanning.
- *Has no analogue:* the page-furniture pass (§13.H of the text design). Sheets
  are not paginated; that machinery is dead weight here.

**G. Function coverage — the durable core comes from measurement, the modern
tail from reasoning** (2026-08-02).

*The core, measured.* Hermans & Murphy-Hill analysed the **Enron corpus** —
16,189 unique spreadsheets recovered from 51,572 Excel files in the email
archive. Only **134 distinct functions** appear in the whole corpus, against
300+ available. The **top nine cover 63.6% of spreadsheets**:

    SUM   +   -   /   *   IF   NOW   AVERAGE   VLOOKUP

and the top fifteen cover 75.6%. (The remaining six of that fifteen are in the
paper's Table VI, which is not openly accessible; not guessed at here.)

Three consequences, none of them obvious before seeing the list:

1. **Four of the top nine are arithmetic operators, not functions.** The
   expression evaluator — precedence, cell references, ranges — carries the
   largest single share of coverage before any function library exists. That
   makes it the right first milestone, and it is testable against cached values
   immediately.
2. **`NOW` in the top nine breaks the oracle.** Volatile functions (`NOW`,
   `TODAY`, `RAND`) cannot be validated against Excel's cached value, because
   that value dates from whenever the workbook last calculated. They must be
   excluded from oracle comparison EXPLICITLY, or the suite is either flaky or
   silently skipping them.
3. **`VLOOKUP` is load-bearing for both engines** — it is the hard one to
   evaluate and it is exactly what lowers to a join in the SQL compiler, so it
   should shape the design early rather than be bolted on.

*The caveat that limits all of this: the corpus is 2001.* It predates `SUMIFS`
(2007) and `XLOOKUP` (2019) entirely. It is evidence about the durable core, not
about modern usage. [FUSE](https://www.researchgate.net/publication/308861425)
(249k spreadsheets from Common Crawl) is the larger, more recent follow-up if
post-2007 data is wanted.

*The modern tail, reasoned rather than measured* — flagged as ESTIMATE, to be
replaced by measurement (below). For CECL and credit-union work specifically,
which is discounted cash flow, vintage analysis and loss rates:

- `IFERROR` (2007) — very common, usually wrapping a lookup. Interacts directly
  with the error value kind the reader already models.
- `SUMIFS` / `COUNTIFS` / `AVERAGEIFS` — conditional aggregation; the natural
  successors to the `SUMPRODUCT` idiom that pervades older finance models.
- `INDEX`/`MATCH` — the lookup pair finance modelling prefers to `VLOOKUP`.
- `XLOOKUP` — in anything authored recently.
- Date math: `EOMONTH`, `EDATE`, `YEARFRAC`, `NETWORKDAYS` — day-count
  conventions are central to amortization.
- Financial: `NPV`, `XNPV`, `IRR`, `XIRR`, `PMT`, `IPMT`, `PPMT`, `RATE`. CECL
  is literally discounted cash flow, so these are likelier here than the Enron
  ranking suggests.
- `ROUND` / `ROUNDUP` / `ROUNDDOWN`, and `TEXT` / `LEFT` / `RIGHT` / `MID`.

*The better corpus is the user's own, and it is obtainable.* A **list of function
names** is not proprietary in the way the data is. Counting the contents of
`<f>` elements across real workbooks yields a ranked list from actual CECL work,
which beats any public corpus for this purpose — and once Stage 1 read exists
(it does), the module can produce that scan itself. Do this before committing to
Phase C/D coverage.

*Phasing:*

| phase | scope | why here |
|---|---|---|
| A | expression evaluator: operators, references, ranges | the largest single share of usage |
| B | `SUM`, `IF`, `AVERAGE`, `ROUND`, `MIN`/`MAX`, `COUNT` | the rest of the durable core |
| C | `VLOOKUP`, then `SUMIF(S)`/`COUNTIF(S)`, `IFERROR`, `INDEX`/`MATCH` | the lookup and conditional-aggregate family; also the compiler's join story |
| D | by demand, from the corpus scan | the tail is thin — 134 total across 16k spreadsheets |

Throughout: **an unknown function refuses loudly** rather than returning a
plausible wrong number. In a financial model a wrong number that looks right is
worse than a failure.

**I. The corpus scan, actually run** (2026-08-03). §G said to measure before
committing to Phase C/D coverage. Done — against the Enron corpus itself rather
than a paraphrase of the paper.

*Method.* The figshare distribution
([10.6084/m9.figshare.1221767](https://doi.org/10.6084/m9.figshare.1221767),
CC BY 4.0) ships the spreadsheets **already converted to `.xlsx`** — 15,871
OOXML files, re-saved through Excel by the researcher in 2014, plus 58 stragglers
still in legacy `.xls`. No conversion was needed, and because Excel wrote these
files their cached values are genuine Excel output, so `xlsx.check` against them
is a true oracle rather than the self-consistency check the hand-built fixture
allows. Each workbook was scanned in its own process (see the DOGFOOD entry:
`xlsx.open` raises and gBASIC cannot catch a raise, so an in-process directory
walk would abort the whole scan on the first bad file). Result: **15,871 / 15,871
read successfully**, 20.7M formula cells, 6.0M function calls, 231 distinct names.

*The scan found exactly one reader defect,* and it was not in the ZIP, the XML or
the cell parsing — those had zero failures across the corpus. It was that a sheet
declared with an empty `r:id` (a VBA module or macro sheet: really in the
workbook, no worksheet part behind it) was listed by `xlsx.sheets` and then
rejected by `xlsx.cells` as "no such sheet". 400 workbooks (2.5%) carry one.
Fixed; `examples/xlsx_macro_sheet_test.bas` pins it.

*The paper's ranking replicates.* Measured independently, by percentage of
formula-bearing workbooks, the top functions are **SUM (73.2%), IF (21.1%),
NOW (16.3%), AVERAGE (11.1%), VLOOKUP (9.3%)** — precisely the five non-operator
entries of the paper's top nine, in the same order. §G's core can be relied on.

*But share-of-calls and breadth-of-use are very different questions,* and only
one of them is about coverage:

| function | % of workbooks | % of all calls |
|---|---|---|
| SUM | 73.2% | 13.8% |
| IF | 21.1% | 29.8% |
| VLOOKUP | 9.3% | 17.0% |
| YEAR / MONTH | 3.3% / 3.8% | 7.2% / 7.1% |
| NOW | 16.3% | 0.07% |

`VLOOKUP`, `YEAR` and `MONTH` look enormous by call count only because a handful
of workbooks use them tens of thousands of times each. `NOW` is the inverse: one
call per workbook, in a sixth of them. Ranking by call count would have built the
wrong things first.

*The metric that decides build order* is neither of those: **what fraction of
workbooks can be recalculated COMPLETELY** — every function in them supported.
A workbook missing one function is not 99% done, it is unrecalculable.

- Formula-bearing workbooks: **9,220** (58.1% of the corpus).
- **3.0% can never be reached**: they call proprietary Enron add-ins (`_XLL.*`)
  or VBA user-defined functions. This is a real ceiling, not a gap to close.
- With the 14 functions already implemented: **56.1% fully recalculable.**
  (863 of those need no functions at all — pure operators, which is §G's point
  about the expression evaluator, confirmed.)
- With **25 more functions: 87.9%.**

Greedy build order — at each step, the function that unlocks the most additional
*fully* recalculable workbooks:

| # | add | → fully recalculable |
|---|---|---|
| 1 | `NOW` | 68.0% |
| 2 | `SUBTOTAL` | 71.4% |
| 3 | `CELL` | 74.7% |
| 4 | `TODAY` | 77.0% |
| 5 | `SUMIF` | 78.1% |
| 6 | `VLOOKUP` | 79.2% |
| 7 | `WEEKDAY` | 80.4% |
| 8 | `TEXT` | 81.2% |
| 9–14 | `EOMONTH`, `ISNA`, `YEAR`, `MONTH`, `CONCATENATE`, `COUNTIF` | 83.7% |
| 15–25 | `NPV`, `PMT`, `AND`, `OR`, `REPT`, `TRANSPOSE`, `HLOOKUP`, `DATE`, `SUMPRODUCT`, `INT`, `LOOKUP` | 87.9% |

Three things this changes:

1. **`NOW` and `TODAY` are the largest single win and are nearly free** — +12 and
   +2.3 points for a date serial. They were already recognised as volatile by the
   evaluator; they just were not *implemented*. But this is precisely the tension
   §G point 2 flagged: implementing them makes those workbooks recalculable while
   making them permanently unverifiable against cached values. Both facts hold,
   and the oracle must keep skipping them.
2. **`CELL` at rank 3 was not on anyone's list** — not in §G's measured core, not
   in its reasoned modern tail. It is cheap (`CELL("filename",…)` in a header
   dominates) and it unlocks 300 workbooks. Measurement beat reasoning here.
3. **`VLOOKUP` drops to rank 6** by this metric though it is 17% of all calls.
   Still worth building early for the reasons in §G — it shapes the SQL
   compiler's join story — but it is not the coverage emergency the call count
   suggests.

*Standing caveat.* This is 2001 usage. `SUMIFS`, `XLOOKUP` and `IFERROR` cannot
appear in it at all, and §G's reasoned modern tail is untouched by this scan —
it remains an ESTIMATE. The scan answers "what does the durable core actually
cost to cover", not "what will a 2026 CECL workbook need".
`tools/xlsx_function_scan.bas` exists to answer the second question against real
workbooks when some are available; it prints only names and counts.

**J. Running the oracle over the corpus — and what counting functions could not
see** (2026-08-09).

§I ranked work by *function name*. Building the top of that list
(`NOW`, `TODAY`, `SUBTOTAL`) and then pointing `xlsx.check` at all 15,871
workbooks produced a result that reorders the roadmap, because the dominant
defects were **not missing functions at all** — and a scan that counts
`NAME(` tokens is structurally incapable of seeing any of them.

*This is now a true oracle.* The figshare files were re-saved through Excel in
2014, so their cached values are Excel's own output. Baseline over the whole
corpus:

| | |
|---|---|
| formula cells judged | 16,902,786 |
| **agree** | **11,166,551 (66.1%)** |
| disagree | 5,736,235 (33.9%) |
| unsupported (named, skipped) | 3,710,388 |
| volatile (skipped) | 121,178 |
| workbooks with ZERO disagreements | 5,169 of 9,220 (56.1%) |

**1. Shared formulas — 61% of workbooks, and we were CORRUPTING them.** Excel
does not repeat a formula filled down a column. It writes the text once and
gives the rest of the run an empty back-reference:

```xml
<c r="C2"><f t="shared" ref="C2:C6" si="0">A2*$B$1</f><v>20</v></c>
<c r="C3"><f t="shared" si="0"/><v>30</v></c>
```

Read naively C3 has "a formula whose text is empty", which evaluates to
`#VALUE!`. Since `xlsx.recalc` writes values back, it **replaced every such cell
with `#VALUE!`** — measured at 171 cells on the first corpus file tried. This is
not a corner: **13.2M of the 20.7M formula cells in the corpus are
continuations**, so nearly two thirds of every formula cell was being read as
empty, and 61.0% of formula-bearing workbooks contain at least one.

Resolving one means translating the anchor's text by the offset between the
cells — relative references shift, absolute (`$`) ones do not, and a
reference-looking substring inside a string literal is text. Getting that wrong
would be *worse* than not doing it, because the result is a plausible number
computed from the wrong cells; hence a fixture with one column per rule
(`examples/xlsx_shared_formula_test.bas`).

*This also means §I's function ranking is measured on a biased sample* — the
scanner only ever saw master and plain formulas, never the two-thirds of cells
whose text lives elsewhere. The ranking is still the best available evidence
about the durable core, but it is not a census of formula cells.

**2. Cross-sheet references — 3.09M disagreements, the single largest cause.**
`Map!$E$106`, `'[1]Date Master'!$B$1`. The evaluator holds one sheet's snapshot
and cannot see another, so every one of these is `#VALUE!`. This is 54% of all
disagreements and is now the top item of remaining work.

**3. A formula yielding an EMPTY cell is 0, not empty** — 351,897 disagreements,
about 6%, fixed by one line. `=Z50` where Z50 is blank displays 0 in Excel.
Empty already coerced to 0 *inside* arithmetic; only the top-level result was
wrong. Confirmed against LibreOffice, along with the three neighbouring cases
that were already right (`Z50+1`→1, `Z50&"x"`→"x", `IF(Z50="",…)`→ the empty
branch).

**4. Defined names — ~145k.** `cappercentile`, `VALUEDATE`, `Tot_Cost`, and bare
names used as operands (`ML`, `NB`). Workbook-level named ranges in
`<definedNames>`, which we do not resolve. Invisible to a function-name scan for
the same reason as everything above: they have no parentheses.

*A performance defect the corpus also exposed.* Both cell lookups — the
evaluator's and the dependency graph's — were linear scans over every cell, run
inside per-formula loops, so the cost was the product. On
`john_griffith__15586__Crude.xlsx` (182,752 cells, 50,343 formulas on one sheet)
`xlsx.check` **did not finish in 300 seconds**, while merely reading the same
file took 0.44s — so the cost was entirely the scans. A `(row,col)` hash index
built once per snapshot took it to **2.7s**, and corpus throughput from 197 to
6,477 workbooks per minute. Guarded by a CEILING rather than a ratio in
`tests/run_xlsx.sh`, for a measured reason: with the linear scan restored the
4x-size step reports 7.7–8.0x, which an 8x ratio gate would have passed.

*Revised order of work,* by measured share of disagreements rather than by
function frequency:

| | cause | disagreements |
|---|---|---|
| 1 | cross-sheet references | 3,092,583 |
| 2 | ~~shared formulas~~ **done** | (was ~2/3 of all formula cells) |
| 3 | ~~empty-cell result~~ **done** | 351,897 |
| 4 | defined names | ~145,000 |
| 5 | the §I function list (`CELL`, `SUMIF`, `VLOOKUP`, …) | the remainder |

The function list is not wrong, but it is no longer the top of the queue: the
structural gaps above account for more disagreeing cells than every missing
function combined.

**K. Post-2001 capabilities, and the second structural blocker** (2026-08-09).

Everything in §G–§J is measured on a 2001 corpus, which cannot contain a single
function added since. That half needed its own fixture, and building it turned
up a blocker of the same kind as shared formulas.

**A modern function is not stored under its own name.** Excel writes anything
newer than the original ECMA-376 function list with a *future-function prefix*,
so an older reader cannot mistake it for something it knows:

```
XLOOKUP(...)      →  _xlfn.XLOOKUP(...)
SORT(...)         →  _xlfn._xlws.SORT(...)      worksheet-only
LET(x, A1, x*2)   →  _xlfn.LET(_xlpm.x, A1, _xlpm.x*2)
```

Until those are stripped, **every** modern function is an unknown name however
well it is implemented. The split is by schema version, not release year, which
is why the 2007-era additions — `IFERROR`, `SUMIFS`, `COUNTIFS`, `AVERAGEIFS`,
`EOMONTH`, `YEARFRAC`, `NETWORKDAYS` — carry no prefix while `STDEV.S` and
`IFNA` do. Established by generating each one and reading the bytes back.

What must **not** be stripped: `_xll.` (a third-party add-in) and `_xludf.` (a
VBA function). Those are unevaluable *in principle* — the 3.0% ceiling of §I —
and stripping them would silently reclassify "impossible" as "not done yet".
`basic.xlsx` now uses `_xll.HPVAL` as its unsupported-function case for exactly
this reason: it previously used `XLOOKUP`, and that test broke the moment
XLOOKUP was implemented — a fixture pinned to a gap that later closed.

*The fixture and its oracle.* `tools/make_xlsx_modern_fixture.sh` drives
**LibreOffice** to compute the cached values in
`examples/fixtures/xlsx/modern.xlsx`. This matters: hand-written expectations,
which `basic.xlsx` is stuck with, only ever prove self-consistency — misread
`SUMIFS` and you write the wrong expected value and the test agrees with you.
Having an independent engine compute them removes precisely that failure mode.

The honest ranking of evidence, which the fixture header states too:

> Excel-authored (the corpus)  >  LibreOffice-authored (this)  >  hand-written

LibreOffice is not Excel and the two are known to differ at the edges, so
agreement is strong evidence rather than proof. The fixture's arithmetic is
also kept small enough to verify by eye (East is 1200 + 1500 + 950, so `SUMIFS`
must be 3650) — a third independent check. Where LibreOffice's own answer was
not clearly right it was *not* matched: see `YEARFRAC` below.

*Implemented and confirmed against that oracle* — 62 formulas, zero
disagreements:

| group | functions |
|---|---|
| conditional aggregation | `SUMIF` `SUMIFS` `COUNTIF` `COUNTIFS` `AVERAGEIF` `AVERAGEIFS` `MAXIFS` `MINIFS` |
| logic | `IFNA` `NA` `XOR` `IFS` `SWITCH` |
| text | `CONCAT` `CONCATENATE` `TEXTJOIN` `TEXTBEFORE` `TEXTAFTER` |
| lookup | `XLOOKUP` `XMATCH` (exact mode) |
| dates | `EOMONTH` `EDATE` `DATE` `DAYS` `YEAR` `MONTH` `DAY` `WEEKDAY` `ISOWEEKNUM` `NETWORKDAYS` `YEARFRAC` |
| statistics | `STDEV(.S/.P)` `VAR(.S/.P)` `MEDIAN` `PERCENTILE(.INC)` `QUARTILE(.INC)` `RANK(.EQ)` `SMALL` `LARGE` `AGGREGATE` |

The criteria matcher these share supports comparison operators, numeric vs text
comparison (`">4"` must match 12 — string comparison would sort `"12"` before
`"4"` and quietly drop it), case-insensitive text, and `*`/`?` wildcards.

*Deliberately refused rather than approximated:*

- **`YEARFRAC` basis 1** (actual/actual). Its denominator rule is intricate;
  getting it subtly wrong yields a plausible interest figure, which is the exact
  failure this module exists to avoid. Bases 0, 2, 3 and 4 are implemented.
- **`XLOOKUP`/`XMATCH` non-exact match modes**, which would otherwise return a
  plausible neighbouring row.
- **`AGGREGATE` reference forms** (function numbers 14–19).

*Deferred, with reasons:*

- **Dynamic arrays** (`UNIQUE`, `SORT`, `FILTER`, `SEQUENCE`). These **spill**
  into cells that do not exist in the file, so they are a structural feature of
  the grid rather than a function to add — closer in size to shared formulas
  than to `SUMIFS`.
- **`LET`/`LAMBDA`**, which need a local binding environment in the evaluator.

*Effect on the 2001 corpus,* which is the independent check that none of this
regressed the durable core (many of these functions predate 2001 too):

| | before | after |
|---|---|---|
| agreement rate | 66.06% | **69.69%** |
| cells agreeing | 11,166,551 | **12,432,251** |
| unsupported | 3,710,388 | **2,773,051** |
| workbooks with zero disagreements | 5,169 | **5,507** of 9,220 |

**L. Cross-sheet references — the largest remaining cause, closed**
(2026-08-10).

§J named this the top item: 3.09M disagreeing cells, 54% of all of them. The
evaluator held one sheet's snapshot and could not see another, so every
`'Rate Table'!B2` was `#VALUE!`. Three populations, measured:

| share | shape | disposition |
|---|---|---|
| 42% | quoted name — `'Nymex hist.'!A:B` | resolved |
| 30% | plain — `Data!$A$1` | resolved |
| 28% | **external** — `[4]CurveFetch!$D$8` | reported unavailable |

An external reference names a **different workbook**, which is not in front of
us. Excel caches a copy of its last-known values, and reading that would have
made the numbers "agree" — but presenting a stale value from a file we do not
have as though it were current is exactly the confident wrong number this
module exists to refuse. It is reported by name instead, which is why
`unsupported` *rises* in the table below while disagreements collapse: those
cells moved from "silently wrong" to "declared unavailable".

The overwhelming consumer is `VLOOKUP`, so `VLOOKUP`/`HLOOKUP`/`INDEX`/`MATCH`
and the `IS*` predicates landed with it. Two traps in that family, both silent
in opposite directions: **`VLOOKUP`'s 4th argument defaults to APPROXIMATE**,
not exact, as does `MATCH`'s 3rd — so assuming exact turns a valid approximate
lookup into `#N/A`, and assuming approximate returns a confidently wrong
neighbouring row. Both modes are implemented.

Also required: **whole-column ranges** (`A:B`, `3:7`), which real lookup tables
are written with because the author does not know how far the data will grow.
`A:B` nominally spans 1,048,576 rows, so an open end is clamped to the sheet's
actual extent.

*Three defects found while building this, each worth recording:*

1. **An off-by-one that a passing test concealed.** `xlsx_parse_ref` stores
   columns 0-based and rows 1-based; the whole-column code used 1-based
   columns. `COUNTA('Rate Table'!A:A)` returned the *right answer for the wrong
   column* — it read column B, which happened to hold four values too. Only
   `AVERAGE(B:B)` (reading empty column C) exposed it.
2. **A dangling pointer that crashed on real data.** The sheet-snapshot pool
   grew by `realloc` while callers held `XlsxSnap*` into it, so the second
   sheet a formula touched could move the first out from under a live pointer.
   One corpus workbook segfaulted. Snapshots are now heap-allocated
   individually. Clean under valgrind.
3. **Materialising a range per lookup is ruinous.** One workbook calls
   `VLOOKUP` twice per formula over `'CP Trade Data'!$D$2:$P$6949` — a
   90,324-cell rectangle — across thousands of formulas: 116s for one file.
   The four functions that address a range *by position* now take an
   unmaterialised descriptor and read only the cells they need, searching one
   column (6,948 indexed lookups) instead of copying the rectangle. **116s →
   12s.**

*Corpus effect,* measured over all 15,871 workbooks:

| | start of session | +post-2001 | +cross-sheet |
|---|---|---|---|
| **agreement rate** | 66.06% | 69.69% | **94.91%** |
| cells agreeing | 11,166,551 | 12,432,251 | **15,760,973** |
| disagreeing | 5,736,235 | 5,407,872 | **840,849** |
| unsupported (declared) | 3,710,388 | 2,773,051 | 3,912,152 |
| workbooks with zero disagreements | 5,169 | 5,507 | **6,779** of 9,220 |

*Known limit at the time, closed in §M below:* `xlsx.recalc` was a per-sheet
operation, so recalculating sheet A after changing sheet B used B's stale
values.

**M. Workbook-wide recalculation** (2026-08-11).

`xlsx.recalc(wb, sheet)` orders one sheet. That was correct while the evaluator
could not see another sheet at all; once §L landed it became a **trap**, because
a formula on one sheet can depend on a *formula* on another, and recalculating
only the first reads the second's stale cached value. Nothing errors — the
number is simply wrong, which is the failure mode this module exists to avoid.

`xlsx.recalc(wb)`, with no sheet, orders across the whole workbook.

*The fixture is built to make a wrong answer visible.* `chain.xlsx` holds

```
Inputs!A1 = 10           a literal
Mid!A1    = Inputs!A1*2
Out!A1    = Mid!A1+1
```

with the sheets declared **Out, Mid, Inputs — the reverse of dependency
order**, so an engine recalculating in sheet order hands `Out` a stale `Mid`.
Set `Inputs!A1` to 100 and the only correct answers are `Mid=200`, `Out=201`; a
stale read gives 21. The test names both numbers, exactly as the single-sheet
D7/B5 case does in §13.D.

*Implementation notes worth keeping:*

- A node is one cell of one sheet, addressed `base[sheet] + position`, so the
  topological order spans sheets and a cycle may run `A!x → B!y → A!x`. Such a
  cycle is **reported, not iterated**, and a healthy cell beside it still
  evaluates — one cycle must not sink a workbook.
- The walk is an **explicit stack, not recursion**. Depth here is the length of
  a dependency chain, which on a real workbook runs to tens of thousands of
  cells; recursing would overflow the stack exactly as the JSON parser once did
  (PLAT-JSON).
- A dependency edge is kept as a **rectangle**, not expanded to cells: a
  whole-column reference covers a million of them. Small rectangles are probed
  by direct lookup; large ones are resolved by scanning the target sheet's own
  cells once and testing membership. Both are bounded.

*Two leaks valgrind caught here,* both pre-existing and both invisible until
workbook recalc made the call count large enough: `xlsx_call` and
`xlsx_primary` each pre-filled their result with an *allocated* error string
that every branch then overwrote. Fixing the first naively — initialising to
an empty value — would have been worse than the leak: empty becomes 0 at the
top level, so a branch that failed to assign would turn an unsupported function
into a plausible zero. The result now starts as an **unassigned sentinel** (the
error kind with no text) that is resolved to `#NAME?` at the end, which is
leak-free *and* keeps the loud refusal. Confirmed behaviour-neutral by
re-running the whole corpus: byte-identical verdicts.

*Corpus state after this work:* 15,871 workbooks scanned, **0 failures**,
agreement **94.97%** (15,923,882 agree / 842,999 disagree), 3,846,293 cells
declared unsupported rather than guessed at.

**N. L2 built — ARI-for-grids** (2026-08-11). `stdlib/grid.bas`, implementing
§5. Pure gBASIC, for the reason ARI is: the reading is done, and what remains
is *policy* — which row is a header, which row is a total — which belongs where
the person who owns the report can read and change it.

*The contract, unchanged from §5:* **automatic when safe, spec when not, and it
tells you which.**

| verb | does |
|---|---|
| `grid.of(wb, sheet)` | a sparse addressable grid: `at`, `kind_at`, `is_blank`, `row_width` |
| `grid.blocks(g)` | contiguous non-blank row runs — the most reliable structural signal a sheet gives |
| `grid.tables(g)` | best-effort candidates, each with a **confidence and its reasons** |
| `grid.extract(g, spec)` | exactly what the spec says; a frame out |

Spec fields: `starts` / `ends` (anchor by content, so inserting rows above
cannot break it — the ARI idea on a grid), `header_row`, `header_rows`,
`first_row` / `last_row`, `break_on_blank`, `drop_totals`, `drop_matching`,
`label_col`, `columns`.

*The fixture is built so a wrong answer is loud.* `messy.xlsx` carries every
irregularity §5 names — a title above the table, a **two-row header whose
parent is written only above the first child** (how Excel stores a merged
cell), a subtotal interleaved with data, a grand total, a trailing note after a
blank row, a second table of different width, and an empty spacer column inside
the first table's span. The correct answer is deliberately not what a
top-left-to-bottom-right reader produces.

Results on it: the automatic path returns **low** confidence and names both
problems ("two-row header?", "contains 2 rows that look like totals"); the
clean sheet returns **high** with no spec at all. The spec path produces
`Region | Q1 Units | Q1 Value | Q2 Units | Q2 Value` — parent carried across the
merged span, spacer column dropped, both totals removed, 4 data rows.

*The assertion that carries the tier* is arithmetic, not a golden: the extracted
column must sum to the figure **the sheet's own TOTAL row** claims. A golden
would happily record a subtotal absorbed as data; this cannot, because
absorbing one makes the sum too large. It is checked against the sheet rather
than a number written in the test.

Deferred: the ARI **text** spec language over grids. The spec is a record for
now, which needs no parser; §5's ambition of one shared notation stands as
future work rather than being half-built.

**O. L3 built — consolidation** (2026-08-11). `stdlib/consolidate.bas`,
implementing §6. Many tapes with the same *meaning* and a different *surface*,
merged into one frame.

`consolidate.merge(sources, spec)` → `{ok, frame, accepted, rejected, notes}`.
A spec column is `{from: [aliases], kind: "text"|"money"|"percent"|"number",
required: bool, scale: "whole"|"fraction"|"infer"}`.

*On those field names,* which changed once and are worth explaining. They were
originally `names` and `kind`, because both of the natural spellings — `from`
and `as` — are reserved words (`load X from`, ARI's `as money`) and a reserved
word could not be a record key at all. The parser change of 2026-08-13 lifted
that, so `from` is now used and `names` is still accepted for compatibility.
`kind` stays, because the lift was only half of what it looks like: a keyword
can be a record **literal key** but still cannot follow a **dot**, so `{as: 1}`
parses while `rule.as` is a syntax error (dot access is resolved in the lexer,
not the grammar — see DOGFOOD 2026-08-15). Spelling the type field `as` would
therefore force `rule["as"]` throughout the library, and `kind` is the more
informative word regardless.

*What it handles:*

- **Aliasing**, fuzzy first: names are normalised to letters and digits, so
  `"Rate (%)"`, `"rate %"` and `"RATE"` collapse together without an entry. The
  alias list exists only for what fuzz cannot reach (`"Note ID"` → `loan_id`).
- **Money**, the same union ARI recognises in print-image reports, because the
  surface chaos is identical and only the substrate differs: `1500`,
  `"$1,500.00"`, `"(1,200.00)"`, `"1,200.00-"`, `"9,000"`; anything else is
  `unknown`, never zero.
- **Required columns** — a source missing one is **rejected by name**, not
  emitted with unknowns. A tape silently short a balance column understates the
  pool, and an understated pool looks exactly like a small one.
- **Provenance** — every output row carries its source. A consolidated figure
  nobody can trace back is not auditable.

*The trap this layer exists for is the percent scale.* `5.25` and `0.0475` are
the same kind of thing written 100x apart, and nothing in either file says
which. Following ARI's **infer to advise, declare to parse**:

- a written `%` settles it outright — nothing is inferred, and the report says
  so rather than claiming an inference;
- otherwise the judgement is made **per column, from every value at once**,
  because a single cell cannot be judged: if any value exceeds 1 the column
  must be whole percents, since no fraction rate can;
- a column whose values all sit at or below 1 is genuinely **ambiguous** —
  fractions, or whole percents under 1% — and is reported as such, with
  `scale:` available to remove the guess. The test shows the note changing from
  `AMBIGUOUS, assumed fraction` to `scale declared as fraction`.

*The assertions that carry the tier are arithmetic, not goldens.* The
consolidated balances must sum to a figure derived from the four tapes' own
values — which also proves the cents survive. (When this tier was written a
display check would additionally have missed them, because `print` rendered
23750.25 as 23750.2; that gap closed with PLAT-NUMFMT on 2026-08-14 and
`print` now shows the value in full. The arithmetic assertion is kept anyway —
it tests the pipeline's result rather than the interpreter's formatter, which
is what this tier is for.) And every normalised rate must land
inside a plausible band: a mis-inferred scale yields a number that is entirely
plausible in isolation, so only a band check catches it.

**P. L4's easy half built — a frame becomes a table** (2026-08-12).
`stdlib/dbframe.bas`. §7 sequences this before the formula compiler and calls
it valuable on its own, which it is: with L2 extracting and L3 consolidating,
the remaining step for any real workflow is getting the result somewhere it can
be queried and joined.

    dbframe.schema(df)                        inferred {name, column, type}
    dbframe.create_sql(table, df)             the DDL, for inspection and diffing
    dbframe.to_table(df, db, table, options)  create and bulk load

*Two refusals, both for the module's standing reason:*

- **A type is never guessed from one value.** It is decided from every value in
  the column: all-whole → `INTEGER`, any fractional → `REAL`, anything mixed →
  `TEXT`. Taking the first value's type and coercing the rest is how `"n/a"`
  becomes `0`, and a zero that should have been a gap changes a total.
  `unknown` becomes `NULL`, which is what it means.
- **SQL is never built by pasting values.** Every value is *bound*; identifiers
  cannot be bound, so they are validated and refused rather than escaped. A
  table name that is not a plain identifier is rejected, and two headings that
  collapse to the same identifier are rejected too — silently overwriting one
  with the other is the quiet version of losing a column.

*The injection tier executes the claim rather than asserting it.* A loan id of
`'); drop table loans;--` is loaded, read back and compared to itself, and the
other table is then counted to prove it still stands. "We use bound parameters"
is a claim only a test can make true.

*Append versus replace has no silent default.* Loading twice appends, which is
right for a growing pool and wrong for a re-run, so the caller chooses. The
append path issues `create table if not exists`; a genuine schema mismatch
still fails loudly from the insert, which is the correct outcome — appending a
tape with different columns to an existing pool should stop, not reshape it.
(The bare `create` on the append path was a real bug the test caught.)

*The exactness tier compares rather than prints:* the loaded total must equal
265550.75. When this was written `print` rendered that as 265551, so a printed
total would have hidden exactly the cents the pipeline exists to preserve;
PLAT-NUMFMT fixed the formatter on 2026-08-14 and it now prints in full. The
comparison stands regardless, because what this tier is asserting is that the
pipeline preserved the value, not that the interpreter can display it.

**L0–L3 and the easy half of L4 are now built.** What remains of §7 is the
formula compiler proper — lowering a column formula to a vectorised frame pass
or to SQL — for which `xlsx.recalc` is the oracle.

**Q. The formula compiler, phase 1** (2026-08-12). `xlsx.to_sql(formula,
mapping)` → `{ok, sql, reason}`. §7's headline, narrowly and honestly scoped.

    IF(C2>0.05, ROUND(B2*C2,2), 0)
      -> CASE WHEN "rate" > 0.05 THEN ROUND("balance" * "rate", 2) ELSE 0 END

It is a **second pass over the same lexer** as the evaluator, with the same
precedence chain, emitting text instead of computing values. Sharing the lexer
is the point: a compiler that parsed the dialect slightly differently would
disagree with the interpreter on inputs neither test covers — and the
interpreter is the oracle it is checked against.

*Lowered:* arithmetic and comparison, `&`, unary minus, postfix `%`, `^` (via
`POWER`), row ranges, `IF`, `AND`/`OR`/`NOT`, `SUM` (row-wise), `MIN`/`MAX`
(multi-argument), `ROUND`, `ABS`, `IFERROR` (→ `COALESCE`), `CONCAT(ENATE)`,
`LEN`, `UPPER`/`LOWER`, and a mapped reference-to-constant so a `$F$1` factor
cell folds in.

*Refused, each because it would otherwise produce a plausible wrong number:*

| shape | why |
|---|---|
| `SUM(B2:B99)` | a row-**spanning** range is an aggregate and changes cardinality |
| `B3*2` on row 2 | a reference to another row is a window function |
| `VLOOKUP`, `INDEX`/`MATCH` | lowers to a **join** — phase 2 |
| `SUMIF`, `SUBTOTAL`, `AGGREGATE` | lowers to a **filtered aggregate** |
| `NOW()` | volatile; no set-based meaning |
| `Other!B2` | needs the other sheet as a table |
| `MIN(B2)` | one argument is SQL's *aggregate* MIN, not the scalar one |

The `mapping` may carry `_row`, pinning the row the formula sits on, so a
reference to a fixed cell **above** the data (`B1` in a formula on row 2) is
caught rather than silently compiling to that row's `B`.

*THE ORACLE TIER IS THE POINT.* The same workbook is evaluated cell by cell by
`xlsx.evaluate` **and** loaded into SQLite and computed by the compiled
expression, then compared row by row. It is the only check that can catch a
compiler which is self-consistently wrong, and it earned its keep immediately:
it caught the compiler lowering `AND` to SQL while the evaluator still reported
`AND` as unimplemented, so the two disagreed on every row of that column.
`AND`/`OR`/`NOT` were then implemented in the evaluator.

*Two bugs the tests caught, both of the exact class this module refuses:*

1. `SUM(B2:D2)` compiled to `COALESCE("balance","rate","term",0)` — SQL's
   *first-non-null*, not a sum. The row range was emitted as one comma-joined
   argument; range expansion had to move into the argument collector, which is
   the only place one argument can become several.
2. A bare defined name consumed the following tokens as if it were a call,
   so `VLOOKUP(A2, X:Y, 2, 0)` was refused with "Y does not lower" instead of
   naming `VLOOKUP`.

*Corpus effect of adding `AND`/`OR`/`NOT`,* reported as measured rather than as
a headline: **410,121 cells moved out of "unsupported" into "judged"**, of
which 380,430 agree. The agreement *rate* therefore dips slightly, 94.97% →
94.92%, because the newly-judged population agrees at about 93% against the
existing 95%. Cells that were previously declared unevaluable are now being
judged and mostly getting the right answer; quoting only the rate would report
that as a regression, and quoting only the agree count would hide the 29,691
new disagreements.

**R. The vectorised in-memory target** (2026-08-12). `xlsx.apply(formula,
mapping, frame)` → an array, one value per row: §7's *other* compile target.
Where `to_sql` hands the work to a database, this runs the formula down a
frame's columns in a single pass and hands back the result column.

*What it is, and what it is not.* One call produces a whole column, with no
workbook, no sheet snapshot and no per-cell dispatch from gBASIC. It is **not**
a compiled expression: the formula is re-lexed per row, because the evaluator
still has no retained AST. So the only claim made for it is the one that can be
measured — against the loop a caller would otherwise write, `xlsx.evaluate` per
row, which rebuilds the sheet snapshot every time:

| | 2,400 rows |
|---|---|
| `xlsx.apply` | **5.3 ms** |
| per-row `xlsx.evaluate` | ~374 ms (projected) |
| | **~70x** |

Asserted as a ratio with the gate at 8x, never an absolute time — an absolute
bound would measure how busy the machine is, and the wide margin means the tier
fails on a lost optimisation rather than on a loaded box.

*The three-way check.* Interpreter, SQL and the vectorised pass must produce
the **same column**. Two agreeing could be two implementations of a single
misunderstanding; three agreeing across genuinely different execution models —
a cell-graph interpreter, a database, and an in-memory pass — is a much
stronger statement, and it is what the tier asserts.

*One gap it exposed:* range expansion required a sheet snapshot, so `SUM(B2:D2)`
silently returned only `B` in frame mode. Frame mode now expands a row range
across the mapped columns, and a row-**spanning** range yields `#REF!` there,
since a frame row cannot see its neighbours — a partial sum would have been the
plausible wrong answer.

*Still to do in §7:* phase 2's joins and filtered aggregates (`VLOOKUP`,
`SUMIF`), which are the two largest refusals.

**S. L2 measured against the corpus** (2026-08-12). Everything above the reader
had been tested only against fixtures written for it — I invented the mess,
then checked I could handle the mess I invented. This is L2 pointed at 15,839
real workbooks instead.

*It did not run at all at first.* `grid.of` indexed cells in a RECORD keyed
`"r,c"`, and a gBASIC record is a **linear-scan association list, not a hash
map**, so building N fields costs O(N²) — measured, 2,000 inserts take 28 ms
and 16,000 take 744 ms. A real 182,000-cell sheet is roughly 1.6×10¹⁰
comparisons; the first workbook tried timed out at 120 s. Rebuilt as sparse
per-row arrays (`rownos` ascending, `rowdata[i]` the cells of that row) with a
binary search for the row and a short scan within it. The public API and every
golden are unchanged.

*Then the actual question: does the confidence mean anything?* Self-checked by
asking how often a table STILL CONTAINS A ROW THAT LOOKS LIKE A TOTAL — the
error the layer exists to prevent:

| confidence | candidates | of which contain a totals row |
|---|---|---|
| high | 41,206 | **0.6%** |
| medium | 21,180 | 1.3% |
| low | 46,250 | **14.9%** |
| none | 257,693 | — (no frame emitted) |

**A 25x separation between high and low.** The heuristic knows when it does not
know, which is the property "automatic when safe" actually needs — and it is
now measured rather than asserted.

*Coverage.* 91.4% of workbooks yield at least one usable candidate. Per
candidate the picture looks worse — 70.3% return `none` — but that is mostly
correct refusal rather than failure: 217,102 of those are "no all-text row
found: cannot name the columns", and most blocks on a real sheet are stray
cells, notes and single rows rather than tables.

*What the wild confirmed about the fixture.* Two-row headers are not exotic:
41,852 candidates (11%) triggered that note, and 39,353 had a title or stub
above the header. Both were in `messy.xlsx` on the strength of the design
document; the corpus says they were the right things to include.

*Honest limits, quantified.* L2 materialises a whole sheet as gBASIC values and
costs about **300x the file size in RAM** — 194 MB peak for a 648 KB workbook —
and takes ~13 s on a 1.4 MB sheet. 32 files (0.2%) failed under 14-way parallel
load and succeed run alone, which is memory pressure, not a defect. The
corpus's largest file (42 MB) does not finish.

**U. An independent implementation opens what we write** (2026-08-12). The one
claim in this module that had never been checked by anything but ourselves.

Every other assertion about our output came from our own reader or from
`unzip -t` — and `unzip -t` only proves the file is a valid ZIP, not a valid
workbook. Demonstrated rather than argued: a ZIP containing a single text file
**passes `unzip -t`** and is not a spreadsheet at all.

The tier writes a cell with our writer, then has **LibreOffice** open the result
and export it, and checks that every awkward shape survived the trip through a
foreign reader: the value written, an XML entity (`Opening & carry`),
non-ASCII (`café`), a sparse blank row, a styled date (serial 45000 arriving as
`03/15/2023`), a boolean, an Excel error, and a formula's cached value.

The standing caveat is unchanged: LibreOffice is not Excel, so this is strong
evidence and not proof, and it does not retire the open question of whether
Excel itself opens our files. What it does retire is the weaker and more
embarrassing possibility — that we had only ever proved *we* could read our own
output. SKIPs when libreoffice is absent; the suite does not require it.

**T. Chasing that cost, and where it actually was** (2026-08-12). Three
measurements, two of which contradicted the guess before them — recorded
because the wrong guesses are the useful part.

1. *Guessed: the per-cell records inside `grid.of`.* Rewrote them as parallel
   per-row arrays (one record per ROW rather than per cell). Result: **194 MB →
   193 MB.** Essentially nothing. The representation was not the cost.
2. *Measured instead of guessed.* `xlsx.cells` alone, doing nothing else, on
   the same file: **192 MB for 78,124 cells, ~2.5 KB per cell.** Practically
   all of it was upstream of L2 entirely — a five-field record per cell, with
   its names, cells and strings.
3. *Fixed at the source.* New `xlsx.grid(wb, sheet)` returns the same cells
   **column-oriented** — per row, three parallel arrays of columns, values and
   kinds — one allocation per row instead of five per cell. `grid.of` is now a
   single call to it.

Result on the same workbook: **`grid.of` costs 0.39 s and 142 MB**, where the
equivalent work previously dominated a ~50 s run. `xlsx.cells` is unchanged and
remains right for reading a few cells with their style and formula.

What is left is *not* the grid, and saying so precisely matters more than the
improvement: the residual memory is the sheet parse itself (libxml2's DOM plus
the snapshot), and the residual time is `grid.tables` eagerly building a frame
for **every** block — on the file above, seven frames over a 66,091-row sheet.
Making `tables` lazy, and streaming the sheet parse rather than building a DOM,
are the two remaining levers; neither is done.

**V. The oracle could not name what it refused, and the roadmap paid for it**
(2026-08-15). §13.J established that ranking work by counting `NAME(` tokens in
formula text is structurally blind — a formula usually holds several functions
and only one is the blocker. The lesson was recorded and the *instrument* was
not fixed: `xlsx.check` reported an `unsupported` COUNT with no name attached,
so the blind method remained the only one available and kept steering for
months under its own documented warning.

`xlsx.check` notes now carry `blocked_by`, the name the evaluator actually
refused. Ranked that way over 15,870 workbooks, the top of the remaining work
was not what §14 said came next:

| blocked cells | function |
|---|---|
| 240,587 | `FIND` |
| 207,757 | `LEFT` |
| 48,767 | `LN` |
| 38,315 | `EXP` |
| 29,312 | `SQRT` |
| 27,518 | `HOUR` |
| 26,896 | `MID` |
| ~14,000 | `VLOOKUP`/`SUMIF` — *the planned Phase 4 work* |

Everything above the line is an ordinary text or math function that had simply
never been written, while the roadmap pointed at joins and filtered aggregates
affecting an order of magnitude fewer cells. The rest of the top of the list is
not implementable at all: Enron's own defined names (`SUMMONTHS`, `FOLIOS`,
`PGDBUCKETS`) and external workbook references, both correctly refused.

Implemented in response: the TEXT family (`LEN`, `LEFT`, `RIGHT`, `MID`, `FIND`,
`SEARCH`, `TRIM`, `SUBSTITUTE`, `REPLACE`, `REPT`, `UPPER`, `LOWER`, `PROPER`,
`EXACT`, `CHAR`, `CODE`, `VALUE`, `T`), the MATH family (`SQRT`, `EXP`, `LN`,
`LOG`, `LOG10`, `POWER`, `INT`, `TRUNC`, `MOD`, `SIGN`, `PI`, `CEILING`,
`FLOOR`, `CEILING.MATH`, `FLOOR.MATH`, `PRODUCT`, `SUMPRODUCT`) and the clock
parts (`HOUR`, `MINUTE`, `SECOND`, `TIME`).

*The distinctions that carry the tier,* each a place where a plausible
implementation is wrong in a way positives do not reveal: `INT` FLOORS while
`TRUNC` truncates, so they differ only on negatives; `MOD` takes the sign of the
DIVISOR like Python and unlike C's `fmod`; Excel's `TRIM` collapses interior
space runs rather than only stripping ends; `FIND` is case-sensitive and
`SEARCH` is not; a `FIND` miss is `#VALUE!` and not `0`, which is why callers
wrap it in `IFERROR`; `MID` with a start below 1 is an ERROR rather than a
clamp; `CEILING`/`FLOOR` round to a MULTIPLE of significance, not to an integer.
Positions are 1-based and counted in characters, so the implementation counts
UTF-8 codepoints — which agree with Excel's UTF-16 units for everything outside
the astral planes.

`_xlfn.CEILING.MATH` is **not** an alias for `CEILING`: significance becomes
optional and a third `mode` argument decides how negatives round. LibreOffice
rewrites a bare `CEILING` into that spelling on export, so a reader knowing only
the legacy names refuses a formula the user typed plainly. And for exactly the
negative cases where the two differ, LibreOffice emits the name and then cannot
evaluate its own output — it caches `#VALUE!` — so the fixture excludes them and
that behaviour follows Microsoft's documentation with **no independent
validation behind it**. That gap is stated rather than papered over.

*What the corpus said afterwards, including the part that looked like a loss.*
Implementing the families moved 768,700 cells out of `unsupported` into
`judged`, and only 85% of them agreed — so the headline rate FELL, 94.92% to
94.49%. Read alone that is a regression; read with the judged total it is the
§13.J pattern again, a newly-judged population agreeing below the existing
average. But sampling the new disagreements found something better than an
explanation: 43,309 of 43,900 sampled were formulas containing `FIND`, nearly
all of one shape —

```
IF(ISNUMBER(FIND("Pow",F11)), <arithmetic using FIND("-",R11)>, Q11-P11+1)
```

— where `F11` is `"US Natural Gas"`, `FIND` correctly returns `#VALUE!`,
`ISNUMBER` correctly returns `FALSE`, and the answer is the else branch. We
returned `#VALUE!`. **`IF` was propagating an error from the branch it did not
take.** Arguments are evaluated before dispatch and `IF` was not in the
error-catching set; Excel evaluates only the chosen branch. `IFS` and `SWITCH`
had the same shape.

The bug is older than this work and could not have been seen before it. The
guard exists precisely to protect a branch that would otherwise fail, so while
`FIND` was unimplemented the whole cell was skipped as `unsupported`.
Implementing `FIND` made the guard work, made the unused branch legitimately
error, and only then did `IF` mishandle it — a latent defect that a missing
feature had been hiding. Each of the three now still propagates an error in its
own CONDITION, since `IF(#VALUE!, a, b)` must remain `#VALUE!` rather than
quietly taking the else branch and returning a plausible number.

| | baseline | text+math | + the `IF` fix |
|---|---|---|---|
| judged | 17,177,002 | 17,945,768 | 17,945,702 |
| agree | 16,304,312 | 16,957,682 | **17,160,915** |
| disagree | 872,690 | 988,086 | **784,787** |
| unsupported | 3,436,172 | 2,667,406 | 2,667,472 |
| agreement | 94.92% | 94.49% | **95.63%** |
| workbooks with zero disagreements | 13,247 | 13,237 | **13,358** |

Absolute disagreements end 87,903 BELOW the baseline while judging 768,700 more
cells. Note that neither the fixture tier (65/65 against LibreOffice) nor any
suite could have found the `IF` defect: the triggering shape is a guard around a
deliberately-failing branch, which is exactly what a fixture author does not
write, having just implemented the functions that make the guard look
unnecessary.

**W. Ranking the disagreements, and the third of them that were not wrong
answers** (2026-08-15). With refusals no longer dominant, the remaining 784,787
disagreements needed an instrument. Ranking them by function name would have
repeated the §13.J mistake in a new place: a cell disagrees because of what the
whole formula did, and its leading function is rarely the cause. Bucketed by the
SHAPE of the mismatch instead:

| computed → cached | cells | |
|---|---|---|
| `err` → num | 478,723 | 61% of all disagreements |
| `err` → err | 165,283 | right to fail, wrong error code |
| num → num | 66,353 | genuine arithmetic difference |
| `err` → text | 41,507 | |
| bool → num | 21,714 | |

and within the first class, `#REF!` → num alone was **272,134 cells, 35% of
every disagreement in the corpus**. A sample was 295 of 295 external workbook
references.

*The cause was one missed spelling.* External references are detected in the
lexer by a leading `[` — the unquoted `[4]CurveFetch!$D$8`. They can also be
QUOTED, and then the bracket sits INSIDE the quotes: `'[1]FINANCIAL PIVOT'!B12`.
Those took the quoted-sheet path, were read as an ordinary sheet named
`[1]FINANCIAL PIVOT`, failed to resolve, and returned `#REF!` **without setting
`unsupported`** — so an unavailable input was scored as a wrong ANSWER. The
evaluator's behaviour was correct throughout (refusing to read Excel's stale
cached copy of a workbook we do not hold); only the classification was wrong.

This is §13.L's shape again, and the reason is structural rather than careless:
quoting is forced by a SPACE in the sheet name, so the two spellings are not
stylistic variants that a test author covers interchangeably — they are decided
by the data, and any real corpus holds both. §13.L had already measured the
quoted form at 42% of the cross-sheet population. The unquoted case was
implemented, tested and correct, and said nothing about the other 42%.

*Where the whole day landed:*

| | session start | + text/math | + the `IF` fix | + quoted external |
|---|---|---|---|---|
| judged | 17,177,002 | 17,945,768 | 17,945,702 | 17,640,192 |
| agree | 16,304,312 | 16,957,682 | 17,160,915 | **17,146,698** |
| disagree | 872,690 | 988,086 | 784,787 | **493,494** |
| unsupported | 3,436,172 | 2,667,406 | 2,667,472 | 2,972,982 |
| agreement | 94.92% | 94.49% | 95.63% | **97.20%** |
| clean workbooks | 13,247 | 13,237 | 13,358 | **14,208 (89.5%)** |

Disagreements fell 43% across the session and workbooks with zero disagreements
rose by 961. Note the last column's `judged` DROPS and `unsupported` RISES by
the same 305,510: that is the reclassification, not lost coverage, and 14,217 of
those cells had previously *agreed* by coincidence — our `#REF!` happening to
match a cached `#REF!` — which is exactly the kind of accidental agreement a
rate alone would have protected.

**X. The `#VALUE!` class, and a lesson about sampling** (2026-08-15). The next
class down was `#VALUE!` against a cached number. Sampling its formulas, the
visible oddities — leading `+`/`-` Lotus syntax, `TRANSPOSE`, Enron's lowercase
UDFs — accounted for barely 1,200 of 25,108 lines. The bulk was one shape:
`(YEAR(Q274)-YEAR(P274))*12+MONTH(Q274)-MONTH(P274)+1`, cached `1`.

`xlsx_civil_from_serial` refused every serial below 1900-03-01, because the 1900
leap-year bug makes the range look ambiguous and answering one day out is worse
than not answering. **An empty cell coerces to serial 0**, so `YEAR`/`MONTH`/
`DAY` of a blank date cell failed, and that formula over blank rows is
everywhere. Excel answers 1.

The range is not ambiguous either — Excel's mapping is wrong about *history*,
not undetermined: 0 is 1900-01-00 (`DAY(0)` really is 0), 1..59 are
1900-01-01..1900-02-28, 60 is the phantom 1900-02-29. This module implements
Excel, so reproducing that is correct rather than a concession. Negatives stay
refused.

| | before | after |
|---|---|---|
| agreement | 97.20% | **97.35%** |
| disagree | 493,494 | **468,174** |
| clean workbooks | 14,208 | 14,214 |

*And the sampling was biased, which is the part worth keeping.* One workbook
went from 503 disagreements to 8, and the sample suggested a class of ~198,000.
The corpus-wide gain was 25,320. The sample was taken with `head -80` over the
dirty-workbook list — **not a sample**, a prefix — and a few large files in it,
all repeating one formula template down tens of thousands of rows, dominated
every count. The defect and fix are real; the estimate of reach was not. A
corpus makes it easy to measure and just as easy to measure the wrong
population, and per-cell counts are especially prone to it because one
templated column can outvote a thousand distinct workbooks. Future sampling
here should stride the list, or count DISTINCT WORKBOOKS affected alongside
cells.

*Also worth recording as a pattern:* both substantive fixes today were
**defensive refusals that were right in principle and wrong in practice** — `IF`
propagating an unused branch's error, and this range refusal. In each case the
strictness guarded a hazard that barely occurs while intercepting ordinary
traffic (a guard formula; a blank cell). That is not an argument against the
refuse-don't-guess rule, which has been repeatedly right, but a refusal's cost
can only be priced by real data — a fixture will never show it, because you
write fixtures for the case you had in mind.

*What is left,* by the ranking taken before these two fixes: the
wrong-error-code classes (~156,000) and genuine arithmetic differences
(66,353). Both figures predate the external-reference and 1900-serial changes,
so **the ranking should be re-run before acting on them** rather than trusted as
current.

## 14. Roadmap (phases)

Each phase is independently shippable and golden-file testable.

### Phase 1 — read + grid (L0 read, L1)
Unzip + XML-backed reader; workbook/sheet/cell/range access; typed cells mapping
to native values; blank⇒unknown; formulas preserved as text. Golden tests over
small `.xlsx` fixtures. Immediately useful: get any sheet's data as typed gBASIC
values.

### Phase 2 — extraction (L2)
`xlsx.tables` heuristic detector (with confidence) and the ARI-for-grids
spec path: header anchoring, multi-row headers, section boundaries, totals
exclusion, blank-break. Emits frames. Golden tests with intentionally messy
fixtures.

### Phase 3 — consolidation + DB load (L3, easy half of L4)
Mapping spec (aliasing, unit/percent normalization, required columns, provenance
merge); schema inference → `CREATE/ALTER TABLE` and bulk load via sqlite/pg.
This closes the CECL consolidation use case end to end. (Writing moved to
Phase 2 — see the reordering note below.)

### Phase 2b — write + round-trip (L0 write)  **[moved up from Phase 3]**
Re-emit the part tree with edits; untouched parts byte-identical. Sequenced
before extraction deliberately: round-trip fidelity is a property of the
*reader* (§13.A), so deferring it means discovering late that the reader
discarded what write needed. The proof is a `read → write → read` test over a
workbook with charts, conditional formatting and a VBA project, asserting the
untouched parts survive byte-for-byte.

### Phase 3b — formula parser + local recalculation
The Excel-formula parser (shared with Phase 4) and an in-process dependency
graph + evaluator. Validated against **Excel's own cached values** on real
workbooks, cell by cell (§13.D). No database required.

### Phase 4 — formula compiler (hard half of L4)
Excel-formula parser for the tabular subset; compile to frame transforms and to
pushed-down SQL (`IF`→`CASE`, `SUMIF`→filtered aggregate, `VLOOKUP`→join);
execute set-based; refuse-with-diagnostic what won't lower. Targets Postgres and
SQLite over a common subset, so local work needs no server. Golden tests assert
generated SQL; integration tests run it; and a CROSS-CHECK tier requires the
compiler and the Phase 3b recalc engine to agree over the same workbook — the
compiler's only real oracle (§13.D). The scaling promise, delivered.
