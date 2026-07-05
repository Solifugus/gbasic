# gBASIC Excel (xlsx) module — design proposal

Status: proposal (not yet implemented)

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

## 13. Open questions for the user

1. **Write path** — `libxlsxwriter` (my lean, correctness) vs. self-emit
   (no new dep, more risk)? This gates L0-write.
2. **Unzip dependency** — `libzip`, `minizip`+zlib, or hand-rolled inflate? Any
   preference given what's typically installed on your targets (incl. the RISC-V
   box)?
3. **Shared ARI engine** — should L2 literally reuse the text library's ARI
   parser/notation (one spec language to learn, some coupling), or a grid-native
   spec that borrows the concepts but stands alone? My lean: share the notation,
   with grid-specific extensions (`header rows`, cell-address anchors).
4. **L4 ambition for v1** — ship L0–L3 + schema-to-DB first and treat the
   formula compiler as a distinct later effort (my strong lean), or commit to a
   formula-subset MVP in the first cut?
5. **Function coverage for the eventual compiler** — which Excel functions are
   the must-haves for your CECL/FinTech work (I'd guess `IF`, `SUMIF(S)`,
   `VLOOKUP`, `ROUND`, date math), so Phase-by-phase coverage matches real use?

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

### Phase 3 — consolidation + write + DB load (L3, L0 write, easy half of L4)
Mapping spec (aliasing, unit/percent normalization, required columns, provenance
merge); `.xlsx` writing; schema inference → `CREATE/ALTER TABLE` and bulk load
via sqlite/pg. This closes the CECL consolidation use case end to end.

### Phase 4 — formula compiler (hard half of L4)
Excel-formula parser for the tabular subset; compile to frame transforms and to
pushed-down SQL (`IF`→`CASE`, `SUMIF`→filtered aggregate, `VLOOKUP`→join);
execute set-based; refuse-with-diagnostic what won't lower. Golden tests assert
generated SQL; integration tests run it. The scaling promise, delivered.
