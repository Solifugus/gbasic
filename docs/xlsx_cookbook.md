# The xlsx cookbook — reading, editing and mining spreadsheets in gBASIC

Status: **every example on this page is a file in `examples/xlsx_cookbook/`, run
by `tests/run_xlsx_cookbook.sh` on every pass.** The code blocks and the output
blocks below are checked byte-for-byte against those files and their recorded
output, so this page cannot drift from what the library actually does. If you
change a recipe, run `tools/sync_xlsx_cookbook.sh` to bring the page back in
step; the suite fails until you do.

Requires a build with **zlib and libxml2** (`xlsx.*`), and for recipes 11–12 also
**sqlite**. Recipes 8–12 load libraries from `stdlib/`, so run them with
`GBASIC_PATH=stdlib`.

```sh
GBASIC_PATH=stdlib ./gbasic examples/xlsx_cookbook/01_open_and_look.bas
```

---

## What this library is for

Most spreadsheet libraries generate new files. This one **edits existing ones**,
which is a much harder guarantee and the reason it exists: the reader keeps every
part of the workbook, including the parts it does not understand, so saving does
not quietly destroy whatever else was in the file.

The second thing it does that most tooling does not: an `.xlsx` stores, for every
formula cell, **the value Excel computed last time**. That makes the file its own
test oracle. `xlsx.check` uses it to tell you whether our formula engine agrees
with Excel on *your* workbook — before you trust anything you compute from it.

The API is fifteen calls:

| Reading | Writing | Formulas | Compiling |
|---|---|---|---|
| `open` `sheets` `dims` | `set` `save` | `evaluate` | `to_sql` |
| `cells` `cell` `grid` | | `check` `recalc` | `apply` |
| `parts` `part` | | | |

Above them sit four gBASIC libraries — `grid`, `consolidate`, `dbframe`, `frame` —
that turn sheets into tables you can query.

---

## 1. Open a workbook and see what is in it

<!--CODE:01_open_and_look-->

```basic
' Recipe 1 — Open a workbook and see what is in it.
'
' `xlsx.open` needs zlib and libxml2 in the build. It returns a HANDLE, not a
' copy: passing it around costs nothing and every call reads the same workbook.

program main(args)
  wb = xlsx.open("examples/fixtures/xlsx/basic.xlsx")

  print "sheets: " + join(xlsx.sheets(wb), ", ")

  ' `dims` reports the USED range of a sheet, not a fixed grid.
  '
  ' MIND THE BASES, they differ: rows are 1-BASED (row 1 is row 1, as Excel
  ' shows it) and columns are 0-BASED (column A is 0). So the range below is
  ' rows 1..7 and columns A..D.
  d = xlsx.dims(wb, "Ledger")
  print "Ledger rows " + d.first_row + "-" + d.last_row + ", cols " + d.first_col + "-" + d.last_col

  ' A workbook is a tree of ZIP parts. The reader keeps ALL of them, including
  ' the ones it does not understand -- that is what makes editing and saving an
  ' existing workbook possible instead of generating a new one.
  print ""
  print "parts (modelled = something interpreted it):"
  for each p in xlsx.parts(wb)
    print "  " + p.name + " bytes=" + p.bytes + " modelled=" + p.modelled
  end for
end program
```

<!--OUT:01_open_and_look-->

```
sheets: Ledger, Notes & Refs, Formulas, Circular
Ledger rows 1-7, cols 0-3

parts (modelled = something interpreted it):
  [Content_Types].xml bytes=1224 modelled=false
  _rels/.rels bytes=296 modelled=false
  xl/workbook.xml bytes=436 modelled=true
  xl/_rels/workbook.xml.rels bytes=988 modelled=true
  xl/sharedStrings.xml bytes=261 modelled=true
  xl/styles.xml bytes=392 modelled=false
  xl/worksheets/sheet1.xml bytes=704 modelled=true
  xl/worksheets/sheet2.xml bytes=241 modelled=false
  xl/worksheets/sheet3.xml bytes=1444 modelled=false
  xl/worksheets/sheet4.xml bytes=324 modelled=false
  xl/customData/vendor.xml bytes=125 modelled=false
```

Two things to carry forward.

**Rows are 1-based, columns are 0-based.** `dims` reports column A as `0`. This
is the single most common thing to get wrong.

**`modelled=false` is not a gap.** It means a part is being carried through
untouched. `xl/customData/vendor.xml` in that listing is a part nothing
understands, and it will survive an edit-and-save intact. That is the whole
design: a reader that discarded it could never write the file back safely.

---

## 2. Cells are sparse — absent is not blank

<!--CODE:02_cells_are_sparse-->

```basic
' Recipe 2 — Cells are sparse, and a missing cell is ABSENT, not blank.
'
' `xlsx.cells` returns only the cells the sheet actually stores. A spreadsheet
' with 40 rows of data in column A does not carry 40 empty cells in column B,
' so DO NOT loop rows x columns and expect a hit every time -- loop the cells
' you were given, or ask for one by reference and test it.

program main(args)
  wb = xlsx.open("examples/fixtures/xlsx/basic.xlsx")

  cells = xlsx.cells(wb, "Ledger")
  print "stored cells: " + count(cells)
  print ""

  for each c in cells
    line = "  " + c.ref + " kind=" + c.kind
    if not is_unknown(c.formula) then
      line = line + " formula=" + c.formula
    end if
    print line + " value=" + string(c.value)
  end for

  ' `kind` is how you tell a number from text from a boolean from an Excel
  ' error. Note "error" is its own kind: #DIV/0! is a VALUE the sheet holds,
  ' not a failure of the read.
  print ""
  print "the sheet has no row 4 and no column C, so those cells are simply not there:"
  print "  C2 present = " + (not is_unknown(xlsx.cell(wb, "Ledger", "C2")))
  print "  A4 present = " + (not is_unknown(xlsx.cell(wb, "Ledger", "A4")))
end program
```

<!--OUT:02_cells_are_sparse-->

```
stored cells: 12

  A1 kind=text value=Account
  B1 kind=text value=Balance
  A2 kind=text value=Opening & carry
  B2 kind=number value=1250.5
  A3 kind=text value=café
  B3 kind=number value=-99.25
  A5 kind=text value=Total
  B5 kind=number formula=SUM(B2:B3) value=1151.25
  A6 kind=number value=45000
  B6 kind=boolean value=true
  A7 kind=error value=#DIV/0!
  D7 kind=number formula=B5*2 value=2302.5

the sheet has no row 4 and no column C, so those cells are simply not there:
  C2 present = false
  A4 present = false
```

The fixture has no row 4 and no column C, and those cells are simply *not
there*. **Do not loop rows × columns expecting a hit every time.** Loop the
cells you were handed, or ask for one by reference and test it with
`is_unknown`.

`kind` distinguishes `text`, `number`, `boolean` and `error`. Note that `error`
is a kind of *value*: `#DIV/0!` is something the sheet legitimately holds, not a
failure of the read.

---

## 3. A formula cell carries two things

<!--CODE:03_formula_cells-->

```basic
' Recipe 3 — A formula cell carries TWO things, and you usually want both.
'
' An .xlsx stores, for every formula cell, the formula text AND the value Excel
' computed the last time it calculated. gBASIC keeps both. That cached value is
' not clutter: it is the only independent check on our own formula engine, and
' `xlsx.check` (recipe 7) is built entirely out of it.

program main(args)
  wb = xlsx.open("examples/fixtures/xlsx/basic.xlsx")

  b5 = xlsx.cell(wb, "Ledger", "B5")
  print "B5 formula      = " + b5.formula
  print "B5 cached value = " + b5.value
  print "B5 kind         = " + b5.kind

  ' `evaluate` ignores the cached value and computes the formula now.
  print "B5 evaluated    = " + xlsx.evaluate(wb, "Ledger", "B5")

  ' A cell with no formula reports `unknown` for it -- test with is_unknown,
  ' do not compare against "".
  b2 = xlsx.cell(wb, "Ledger", "B2")
  print ""
  print "B2 has a formula = " + (not is_unknown(b2.formula))
  print "B2 value         = " + b2.value

  ' Reading a formula does not require the sheet to be recalculated, and
  ' evaluating one does not write anything back. Recipe 8 covers writing
  ' computed values into the workbook.
end program
```

<!--OUT:03_formula_cells-->

```
B5 formula      = SUM(B2:B3)
B5 cached value = 1151.25
B5 kind         = number
B5 evaluated    = 1151.25

B2 has a formula = false
B2 value         = 1250.5
```

`c.formula` is the formula text; `c.value` is Excel's cached result.
`xlsx.evaluate` ignores the cache and computes now. A cell with no formula
reports `unknown` — test with `is_unknown`, not `= ""`.

Keeping both halves is what makes the next recipe possible.

---

## 4. Edit a cell and save

<!--CODE:04_edit_and_save-->

```basic
' Recipe 4 — Change a cell and save, without damaging the rest of the workbook.
'
' `xlsx.set` writes into the in-memory workbook; `xlsx.save` writes a new file
' and returns the byte count. Saving rebuilds ONLY the parts that changed --
' every other part, including ones gBASIC does not model, is copied through
' byte-for-byte. That is why editing a real workbook is safe here and why a
' library that can only generate new files was not enough.

program main(args)
  out = "examples/tmp_cookbook_edit.xlsx"

  wb = xlsx.open("examples/fixtures/xlsx/basic.xlsx")
  print "before: B2 = " + xlsx.cell(wb, "Ledger", "B2").value

  ' Types are preserved: a number stays a number, a string a string.
  xlsx.set(wb, "Ledger", "B2", 9999.99)
  xlsx.set(wb, "Ledger", "A1", "Renamed")
  xlsx.set(wb, "Ledger", "B6", false)

  n = xlsx.save(wb, out)
  print "wrote " + n + " bytes"

  ' Re-open the file we just wrote and confirm it round-tripped.
  wb2 = xlsx.open(out)
  print ""
  print "after:  A1 = " + xlsx.cell(wb2, "Ledger", "A1").value
  print "after:  B2 = " + xlsx.cell(wb2, "Ledger", "B2").value
  print "after:  B6 = " + xlsx.cell(wb2, "Ledger", "B6").value

  ' The part nothing models survived the round trip untouched.
  print ""
  print "vendor part still present = " + (not is_unknown(xlsx.part(wb2, "xl/customData/vendor.xml")))
  print "sheets unchanged          = " + join(xlsx.sheets(wb2), ", ")

  ' Saves are byte-deterministic: the ZIP timestamps are fixed rather than
  ' taken from the clock, so saving the same workbook twice gives identical
  ' bytes and file comparison stays meaningful.
  '
  ' Cleanup. `delete` needs a FILE REFERENCE, not a path string -- the `(file)`
  ' modifier is how a string becomes one.
  tmp(file)= out
  if exists(tmp) then
    delete(tmp)
  end if
end program
```

<!--OUT:04_edit_and_save-->

```
before: B2 = 1250.5
wrote 4050 bytes

after:  A1 = Renamed
after:  B2 = 9999.99
after:  B6 = false

vendor part still present = true
sheets unchanged          = Ledger, Notes & Refs, Formulas, Circular
```

`xlsx.save` rebuilds only the parts that changed and copies everything else
through byte-for-byte — which is why the vendor part is still there afterwards.

Saves are **byte-deterministic**: ZIP timestamps are fixed rather than taken from
the clock, so saving the same workbook twice produces identical bytes and file
comparison stays meaningful.

---

## 5. What writing refuses

<!--CODE:05_what_writing_refuses-->

```basic
' Recipe 5 — What writing refuses, and why each refusal is a feature.
'
' Every refusal below exists to stop a SILENT wrong answer. Catching them needs
' `on error goto next`, then read `error.message` and call `error.clear()`
' before the next attempt.

program main(args)
  wb = xlsx.open("examples/fixtures/xlsx/basic.xlsx")

  on error goto next

  ' 1. Overwriting a FORMULA cell. The cell holds a formula and Excel's cached
  '    value. Writing a literal over it would leave the formula in place, so
  '    Excel would silently revert your edit on its next recalculation. Change
  '    the inputs and recalc instead (recipe 8).
  xlsx.set(wb, "Ledger", "B5", 1)
  print "overwrite a formula cell: " + error.message
  error.clear()

  ' 2. Creating a cell that does not exist. The sheet is sparse, and inventing
  '    a cell means guessing its style and its place in the row order, so a new
  '    cell is refused rather than placed wrongly.
  xlsx.set(wb, "Ledger", "Z99", 1)
  print "create a new cell:        " + error.message
  error.clear()

  ' 3. A sheet that is not there. Note this is a genuine miss -- a name that
  '    IS in the workbook but has no worksheet behind it (a macro sheet) reads
  '    as empty rather than erroring.
  xlsx.cells(wb, "No Such Sheet")
  print "unknown sheet:            " + error.message
  error.clear()
end program
```

<!--OUT:05_what_writing_refuses-->

```
overwrite a formula cell: xlsx.set: refusing to overwrite a formula cell; the formula would recalculate and revert the value
create a new cell:        xlsx.set: creating a new cell is not supported yet; only existing cells can be written
unknown sheet:            xlsx: no such sheet: No Such Sheet
```

Each refusal prevents a specific silent wrong answer:

- **Overwriting a formula cell** would leave the formula in place, so Excel
  would revert your edit on its next recalculation and you would never see it.
  Change the inputs and recalculate instead (recipe 7).
- **Creating a new cell** means guessing its style and its position in a sparse
  row. Refused rather than placed wrongly.
- **An unknown sheet** is a genuine miss. Note a name that *is* in the workbook
  but has no worksheet behind it — a macro sheet — reads as empty instead, since
  that is what it is.

Catching these needs `on error resume next`, then `error.message` and
`error.clear()`.

---

## 6. Check the engine against Excel's own answers

<!--CODE:06_check_the_oracle-->

```basic
' Recipe 6 — Check our formula engine against Excel's own answers.
'
' This is the single most useful call in the library and it has no equivalent
' in most spreadsheet tooling. Because every formula cell stores Excel's cached
' result, each formula can be checked IN ISOLATION -- no dependency graph, no
' recalculation -- by evaluating it and comparing. Run it on a workbook you were
' given, before trusting anything computed from it.

program main(args)
  wb = xlsx.open("examples/fixtures/xlsx/basic.xlsx")

  r = xlsx.check(wb, "Formulas")
  print "agree            = " + r.agree
  print "disagree         = " + r.disagree
  print "volatile skipped = " + r.volatile_skipped
  print "unsupported      = " + r.unsupported

  ' The three non-agreeing outcomes mean different things, and the distinction
  ' matters more than the totals:
  '
  '   disagree    -- we computed a DIFFERENT answer. A real defect, ours or a
  '                  misunderstanding of the function.
  '   volatile    -- NOW/TODAY/RAND. The cached value dates from whenever the
  '                  workbook last calculated, so it is not an oracle. Skipped
  '                  rather than judged.
  '   unsupported -- a function we do not implement. Reported BY NAME so the
  '                  gap is visible, never defaulted to a plausible zero.
  '
  ' Anything not agreeing is listed, never silently counted:
  print ""
  for each n in r.notes
    print "  " + n.verdict + " " + n.ref + "  " + n.formula
    ' `blocked_by` names the function that caused an `unsupported` verdict. It
    ' is present but EMPTY on the other verdicts, so test the string, not
    ' is_unknown -- this is what makes an unsupported function a to-do item
    ' with a name on it rather than an anonymous count.
    if not is_unknown(n.blocked_by) and n.blocked_by != "" then
      print "      blocked_by=" + n.blocked_by
    end if
  end for
end program
```

<!--OUT:06_check_the_oracle-->

```
agree            = 17
disagree         = 0
volatile skipped = 1
unsupported      = 1

  volatile B18  NOW()
  unsupported B19  _xll.HPVAL(A1)
      blocked_by=_XLL.HPVAL
```

This is the most useful call in the library. Because every formula cell stores
Excel's result, each formula is checkable **in isolation** — no dependency
graph, no recalculation.

The three non-agreeing verdicts mean different things, and the distinction
matters more than the totals:

| Verdict | Meaning |
|---|---|
| `disagree` | We computed a **different** answer. A real defect. |
| `volatile` | `NOW`/`TODAY`/`RAND`. The cached value dates from whenever the workbook last calculated, so it is not an oracle. Skipped, not judged. |
| `unsupported` | A function we do not implement, named in `blocked_by` — never defaulted to a plausible zero. |

`blocked_by` is present but **empty** on the other verdicts, so test the string
rather than `is_unknown`.

---

## 7. Recalculate after an edit — and mind the ordering

<!--CODE:07_recalc_after_an_edit-->

```basic
' Recipe 7 — Recalculate after changing an input, and mind the ordering.
'
' `xlsx.recalc(wb, sheet)` recalculates one sheet. `xlsx.recalc(wb)` -- with NO
' sheet argument -- orders across the whole workbook, and that is the one you
' almost always want: a formula can depend on a formula on ANOTHER sheet, and
' the per-sheet form will happily read that other sheet's stale cached value.
' Nothing errors; the number is just wrong.

program main(args)
  wb = xlsx.open("examples/fixtures/xlsx/basic.xlsx")

  print "B5 = SUM(B2:B3), and D7 = B5*2 -- but D7 sits ABOVE B5 in sheet order."
  print "before: B2=" + xlsx.cell(wb, "Ledger", "B2").value + " B5=" + xlsx.cell(wb, "Ledger", "B5").value + " D7=" + xlsx.cell(wb, "Ledger", "D7").value

  ' Change an input.
  xlsx.set(wb, "Ledger", "B2", 1000)

  r = xlsx.recalc(wb, "Ledger")
  print ""
  print "evaluated=" + r.evaluated + " changed=" + r.changed + " circular=" + r.circular
  print "after:  B2=" + xlsx.cell(wb, "Ledger", "B2").value + " B5=" + xlsx.cell(wb, "Ledger", "B5").value + " D7=" + xlsx.cell(wb, "Ledger", "D7").value
  print ""
  print "D7 is 1801.5, not 2302.5 -- it was recomputed from the NEW B5, not the"
  print "stale one, because recalc walks dependencies rather than sheet order."

  ' A circular reference is REPORTED, not iterated toward a fixed point. Healthy
  ' cells on the same sheet still evaluate: one cycle must not sink a sheet.
  c = xlsx.recalc(wb, "Circular")
  print ""
  print "Circular sheet: evaluated=" + c.evaluated + " circular=" + c.circular
end program
```

<!--OUT:07_recalc_after_an_edit-->

```
B5 = SUM(B2:B3), and D7 = B5*2 -- but D7 sits ABOVE B5 in sheet order.
before: B2=1250.5 B5=1151.25 D7=2302.5

evaluated=2 changed=2 circular=0
after:  B2=1000 B5=900.75 D7=1801.5

D7 is 1801.5, not 2302.5 -- it was recomputed from the NEW B5, not the
stale one, because recalc walks dependencies rather than sheet order.

Circular sheet: evaluated=1 circular=2
```

The fixture is built to make the trap visible: `D7 = B5*2` sits **above**
`B5 = SUM(B2:B3)` in sheet order. An engine that evaluated top to bottom would
hand `D7` a stale `B5` and print `2302.5` — a perfectly plausible number.
Dependency-ordered recalculation gives `1801.5`.

**Prefer `xlsx.recalc(wb)` with no sheet argument.** The per-sheet form became a
trap once cross-sheet references worked: a formula can depend on a formula on
another sheet, and recalculating only its own sheet reads the other's stale
cached value. Nothing errors; the number is just wrong.

A circular reference is **reported, not iterated** toward a fixed point, and
healthy cells on the same sheet still evaluate — one cycle must not sink a sheet.

---

## 8. Turn a sheet into a data frame

<!--CODE:08_sheet_to_frame-->

```basic
' Recipe 8 — Turn a worksheet into a data frame.
'
' A sheet is a grid of cells; a frame is columns with names. `grid.of` wraps a
' sheet, `grid.extract` pulls a table out of it according to a SPEC. Needs
' GBASIC_PATH to find stdlib (grid.bas loads frame.bas).

program main(args)
  load grid
  load frame

  wb = xlsx.open("examples/fixtures/xlsx/messy.xlsx")

  ' The "Clean" sheet is the easy case: a header row, then data.
  g = grid.of(wb, "Clean")
  r = grid.extract(g, { header_row: 1 })

  print "ok = " + r.ok
  print "columns: " + join(frame.columns(r.frame), " | ")
  print "rows: " + frame.shape(r.frame)[0]
  print ""
  for each row in frame.to_rows(r.frame)
    print "  " + row["Item"] + "  qty " + row["Qty"] + "  @ " + row["Price"]
  end for

  ' Columns come back as real values, not text: Qty is a number here, so it
  ' sums without parsing.
  print ""
  print "total units = " + sum(r.frame["Qty"])

  ' A spec that matches nothing reports ok=false rather than handing back an
  ' empty frame -- "no data" and "I could not find the table" are different
  ' answers and only one of them is a bug in your spec.
  print ""
  miss = grid.extract(g, { starts: "No Such Anchor", header_row: 1 })
  print "spec matching nothing: ok = " + miss.ok
end program
```

<!--OUT:08_sheet_to_frame-->

```
ok = true
columns: Item | Qty | Price
rows: 3

  bolt  qty 10  @ 0.25
  nut  qty 20  @ 0.1
  washer  qty 5  @ 0.05

total units = 35

spec matching nothing: ok = false
```

`grid.of` wraps a sheet; `grid.extract` pulls a table out of it according to a
spec. Columns come back as real values, so `Qty` sums without parsing.

A spec that matches nothing reports `ok=false` rather than returning an empty
frame. "No data" and "I could not find your table" are different answers and
only one of them is a bug in your spec.

---

## 9. A messy sheet: guess, but read the confidence

<!--CODE:09_messy_sheet-->

```basic
' Recipe 9 — A messy sheet: let it guess, but read the confidence.
'
' Real reports have a title above the table, a two-row header, subtotals mixed
' into the data, and a note underneath. `grid.tables` guesses where the tables
' are and returns a CONFIDENCE plus the reasons it is unsure. A detector that
' were confident about a sheet like this would be worse than none.

program main(args)
  load grid
  load frame

  wb = xlsx.open("examples/fixtures/xlsx/messy.xlsx")
  g = grid.of(wb, "Report")

  ' A blank row is the most reliable structural signal a sheet gives.
  print "blocks the blank rows reveal:"
  for each b in grid.blocks(g)
    print "  rows " + b.first_row + ".." + b.last_row
  end for

  print ""
  print "the automatic guess, with its reasons:"
  for each t in grid.tables(g)
    line = "  rows " + t.first_row + ".." + t.last_row + " confidence " + t.confidence
    if is_unknown(t.header_row) then
      line = line + " (no header)"
    else
      line = line + " header row " + t.header_row
    end if
    print line
    for each n in t.notes
      print "      ! " + n
    end for
  end for

  ' When the guess is low-confidence, STOP GUESSING and write a spec. This one
  ' says: header starts at row 4 and spans 2 rows (Excel writes a merged parent
  ' only above the first child, so joining the levels is what stops two columns
  ' both being called "Units"), and drop the subtotal and grand-total rows,
  ' which a naive read would add to the data and double-count.
  print ""
  r = grid.extract(g, { header_row: 4, header_rows: 2, drop_totals: true })
  print "spec: ok=" + r.ok + " rows " + r.first_row + ".." + r.last_row
  print "columns: " + join(frame.columns(r.frame), " | ")

  ' The assertion that matters is ARITHMETIC, not a printed table: the extracted
  ' column must sum to the figure the SHEET'S OWN total row claims. If a
  ' subtotal had been absorbed as data the sum would be too big -- and would
  ' still look like a perfectly ordinary number.
  print ""
  print "sum of extracted Q1 Units = " + sum(r.frame["Q1 Units"])
  print "the sheet's own TOTAL row = " + grid.at(g, 11, 1)
end program
```

<!--OUT:09_messy_sheet-->

```
blocks the blank rows reveal:
  rows 1..2
  rows 4..11
  rows 13..13
  rows 15..19

the automatic guess, with its reasons:
  rows 1..2 confidence none (no header)
      ! no all-text row found: cannot name the columns
  rows 4..11 confidence low header row 4
      ! the row below also looks like a header: two-row header? pass header_rows: 2
      ! contains 2 row(s) that look like totals: pass drop_totals: true
  rows 13..13 confidence none (no header)
      ! single filled cell: a title or note, not a table
  rows 15..19 confidence medium header row 16
      ! rows above the header were skipped (title or stub)

spec: ok=true rows 6..11
columns: Region | Q1 Units | Q1 Value | Q2 Units | Q2 Value

sum of extracted Q1 Units = 340
the sheet's own TOTAL row = 340
```

Real reports have a title above the table, a two-row header, subtotals mixed
into the data and a note underneath. `grid.tables` guesses and returns a
**confidence with reasons**. A detector that was confident about a sheet like
this would be worse than none.

When the guess is low-confidence, stop guessing and write a spec. `header_rows: 2`
joins the header levels, because Excel writes a merged parent only above the
first child — without it two columns would both be called `Units`. `drop_totals`
removes the subtotal and grand total, which a naive read would add to the data
and double-count.

**The check that matters is arithmetic, not a printed table.** The extracted
column sums to 340, which is what the sheet's own TOTAL row claims. Had a
subtotal been absorbed as data the sum would be too big — and would still look
like a perfectly ordinary number. A golden would have recorded the wrong figure
as expected output; the comparison against the sheet cannot.

Anchor by content (`starts:`) rather than row number where you can, so inserting
rows above a table does not break the spec.

---

## 10. Merge several sheets that mean the same thing

<!--CODE:10_consolidate_many_sheets-->

```basic
' Recipe 10 — Merge several sheets that mean the same thing but look different.
'
' Four loan tapes from four sources. Same meaning, different column names,
' different money formatting, one of them missing a column it must have.
' `consolidate.merge` maps them onto one schema you declare.

program main(args)
  load grid
  load consolidate
  load frame

  wb = xlsx.open("examples/fixtures/xlsx/tapes.xlsx")

  sources = []
  for each s in xlsx.sheets(wb)
    r = grid.extract(grid.of(wb, s), { header_row: 1 })
    print s + ": " + join(frame.columns(r.frame), " | ")
    append(sources, { name: s, frame: r.frame })
  end for

  ' `from` lists the aliases a column may arrive under. Matching is fuzzy --
  ' names are reduced to letters and digits -- so "Rate (%)" finds "rate".
  ' `required: true` means a source LACKING that column is rejected outright
  ' rather than emitted with blanks: a tape missing its balance understates
  ' the pool, and an understated pool just looks like a small one.
  spec = { columns: {
             loan_id: { from: ["Loan #", "Note ID", "loan_number"], kind: "text", required: true },
             balance: { from: ["Balance", "Principal", "Amount"], kind: "money", required: true },
             rate:    { from: ["Int Rate", "Interest Rate", "Rate (%)"], kind: "percent" } },
           source_column: "tape" }

  res = consolidate.merge(sources, spec)
  print ""
  print "ok       = " + res.ok + "   (false: a source was rejected)"
  print "accepted = " + join(res.accepted, ", ")
  print "rejected = " + join(res.rejected, ", ")

  print ""
  print "what it decided, and why:"
  for each n in res.notes
    print "  " + n
  end for

  ' THE TRAP is percent scale: 5.25 and 0.0475 are the same kind of thing 100x
  ' apart and nothing in the file says which. A written % settles it; otherwise
  ' the judgement is made per COLUMN from all values at once, and a column that
  ' could be read either way is reported AMBIGUOUS rather than guessed. Pass
  ' `scale:` to remove the guess.
  print ""
  print "every row carries its source, because an untraceable figure is not auditable:"
  for each row in frame.to_rows(res.frame)
    print "  " + row.tape + "  " + row.loan_id + "  balance " + row.balance + "  rate " + row.rate
  end for
end program
```

<!--OUT:10_consolidate_many_sheets-->

```
CU_A: Loan # | Balance | Int Rate | Orig Date
CU_B: Note ID | Principal | Interest Rate | Origination
CU_C: loan_number | Amount | Rate (%) | Date
CU_D: Loan # | Int Rate

ok       = false   (false: a source was rejected)
accepted = CU_A, CU_B, CU_C
rejected = CU_D

what it decided, and why:
  CU_A.rate: inferred whole (a value exceeds 1, which no fraction rate can)
  CU_B.rate: AMBIGUOUS, assumed fraction (every value is at or below 1: fractions, or whole percents under 1% -- declare `scale` to be sure)
  CU_C.rate: inferred whole (the values carry a written % sign)
  CU_D: REJECTED, missing required column(s) balance

every row carries its source, because an untraceable figure is not auditable:
  CU_A  A-100  balance 150000  rate 0.0525
  CU_A  A-101  balance 82500.5  rate 0.04875
  CU_B  B-200  balance 1500  rate 0.0475
  CU_B  B-201  balance 23750.25  rate 0.0525
  CU_C  C-300  balance -1200  rate 0.06
  CU_C  C-301  balance 9000  rate 0.065
```

Four loan tapes, four sets of column names, one missing a column it must have.
`from` lists the aliases; matching is fuzzy (names reduce to letters and digits,
so `Rate (%)` finds `rate`).

`required: true` means a source lacking that column is **rejected by name**
rather than emitted with blanks. A tape missing its balance understates the
pool, and an understated pool merely looks like a small one.

**The percent-scale trap.** `5.25` and `0.0475` are the same thing 100× apart
and nothing in the file says which. A written `%` settles it; otherwise the
judgement is made per *column* from all values at once — a fraction cannot
exceed 1 — and a column that could be read either way is reported `AMBIGUOUS`
rather than guessed. Pass `scale:` to remove the guess. Note `CU_B` above says
exactly that.

Every row carries its source, because a consolidated figure nobody can trace
back to its tape is not auditable.

---

## 11. Load a frame into SQLite

<!--CODE:11_frame_to_sqlite-->

```basic
' Recipe 11 — Load a frame into SQLite and query it.
'
' The payoff of the whole pipeline: four incompatible spreadsheets become one
' table a query can answer questions about. Needs sqlite in the build.

program main(args)
  load grid
  load consolidate
  load dbframe
  load frame

  wb = xlsx.open("examples/fixtures/xlsx/tapes.xlsx")
  sources = []
  for each s in xlsx.sheets(wb)
    r = grid.extract(grid.of(wb, s), { header_row: 1 })
    append(sources, { name: s, frame: r.frame })
  end for
  spec = { columns: {
             loan_id: { from: ["Loan #", "Note ID", "loan_number"], kind: "text", required: true },
             balance: { from: ["Balance", "Principal", "Amount"], kind: "money", required: true },
             rate:    { from: ["Int Rate", "Interest Rate", "Rate (%)"], kind: "percent" } },
           source_column: "tape" }
  merged = consolidate.merge(sources, spec)

  ' A spreadsheet heading is not a SQL identifier. The mapping is RETURNED so
  ' you can see it -- a quiet rename is how a report ends up joined on the
  ' wrong column. An unsafe name is refused, not escaped.
  print "headings become identifiers:"
  for each h in ["Q1 Units", "Rate (%)", "Loan #"]
    print "  " + h + " -> " + dbframe.to_identifier(h)
  end for

  db = sqlite.connect(":memory:")

  ' Column TYPE is decided from EVERY value, never from the first one: all
  ' whole -> INTEGER, any fractional -> REAL, mixed -> TEXT, unknown -> NULL.
  ' Deciding from the first is how "n/a" becomes 0, and a zero that should
  ' have been a gap changes a total.
  out = dbframe.to_table(merged.frame, db, "loans", { replace: true })
  print ""
  print "loaded ok = " + out.ok + "  rows = " + out.rows
  print out.sql

  print ""
  for each row in sqlite.query(db, "select tape, count(*) n from loans group by tape order by tape", [])
    print "  " + row.tape + "  loans " + row.n
  end for

  ' Assert exactness by COMPARING, not by printing: the point of the pipeline
  ' is that the cents survive it.
  t = sqlite.query(db, "select sum(balance) s from loans", [])[0].s
  print ""
  print "total balance = " + t
  print "exact         = " + (t = 265550.75)
end program
```

<!--OUT:11_frame_to_sqlite-->

```
headings become identifiers:
  Q1 Units -> q1_units
  Rate (%) -> rate
  Loan # -> loan

loaded ok = true  rows = 6
create table "loans" (
  "tape" TEXT,
  "loan_id" TEXT,
  "balance" REAL,
  "rate" REAL
)

  CU_A  loans 2
  CU_B  loans 2
  CU_C  loans 2

total balance = 265550.75
exact         = true
```

Column **type is decided from every value**, never from the first: all whole →
`INTEGER`, any fractional → `REAL`, mixed → `TEXT`, unknown → `NULL`. Deciding
from the first value is how `n/a` becomes `0`, and a zero that should have been
a gap changes a total.

Values are **bound**, never pasted into SQL. Identifiers cannot be bound, so an
unsafe table or column name is **refused rather than escaped**, and the heading →
identifier mapping is returned so you can see it — a quiet rename is how a report
ends up joined on the wrong column.

Note the exactness check is a **comparison**, not a printed number. The point of
the pipeline is that the cents survive it.

---

## 12. Compile a column formula instead of walking cells

<!--CODE:12_compile_a_formula-->

```basic
' Recipe 12 — Compile a column formula instead of evaluating it per cell.
'
' A column formula is ONE transformation applied down a column, so it does not
' need a cell-by-cell walk. `xlsx.to_sql` lowers it to SQL the database runs
' once over the whole table; `xlsx.apply` runs it over an in-memory frame.

program main(args)
  load grid
  load dbframe
  load frame

  ' The mapping says which column letter is which database column. `_row` pins
  ' the formula's own row, so a reference to a FIXED cell above the data is
  ' caught rather than silently compiled to that row's column.
  mapping = { A: "id", B: "balance", C: "rate", D: "term", F1: 0.02, _row: 2 }

  print "what lowers:"
  for each f in ["B2*0.02", "IF(C2>0.05, B2*0.02, 0)", "ROUND(B2*C2, 2)", "SUM(B2:D2)", "B2*$F$1"]
    r = xlsx.to_sql(f, mapping)
    print "  " + f
    print "      " + r.sql
  end for

  ' The refusals are the feature. Every one of these COULD be lowered to
  ' something plausible, and plausible-and-wrong on every row of a column is
  ' the worst outcome available.
  print ""
  print "what is refused, and why:"
  for each f in ["VLOOKUP(A2, X:Y, 2, 0)", "SUMIF(B2:B99, \">0\", C2:C99)", "NOW()", "Other!B2", "MIN(B2)"]
    r = xlsx.to_sql(f, mapping)
    print "  " + f
    print "      " + r.reason
  end for

  ' Run it for real, and check the compiled SQL against the interpreter.
  wb = xlsx.open("examples/fixtures/xlsx/formulacol.xlsx")
  r = grid.extract(grid.of(wb, "Loans"), { header_row: 1 })
  db = sqlite.connect(":memory:")
  dbframe.to_table(r.frame, db, "loans", { replace: true })

  f = xlsx.cell(wb, "Loans", "E2").formula
  c = xlsx.to_sql(f, mapping)
  print ""
  print "column E formula = " + f
  print "compiled         = " + c.sql

  rows = sqlite.query(db, "select id, (" + c.sql + ") v from loans order by id", [])
  agree = 0
  i = 0
  while i < count(rows)
    interp = xlsx.evaluate(wb, "Loans", "E" + (i + 2))
    if abs(number(interp) - number(rows[i].v)) < 0.0000001 then
      agree = agree + 1
    end if
    i = i + 1
  end while
  print "rows where compiled SQL agrees with the interpreter: " + agree + "/" + count(rows)

  ' `xlsx.apply` is the other target: one call, one value per row, no database
  ' and no workbook snapshot per cell.
  vec = xlsx.apply(f, mapping, r.frame)
  print "xlsx.apply gives " + count(vec) + " values, first = " + vec[0]
end program
```

<!--OUT:12_compile_a_formula-->

```
what lowers:
  B2*0.02
      "balance" * 0.02
  IF(C2>0.05, B2*0.02, 0)
      CASE WHEN "rate" > 0.05 THEN "balance" * 0.02 ELSE 0 END
  ROUND(B2*C2, 2)
      ROUND("balance" * "rate", 2)
  SUM(B2:D2)
      (COALESCE("balance", 0) + COALESCE("rate", 0) + COALESCE("term", 0))
  B2*$F$1
      "balance" * 0.02

what is refused, and why:
  VLOOKUP(A2, X:Y, 2, 0)
      VLOOKUP lowers to a JOIN, which this phase does not do
  SUMIF(B2:B99, ">0", C2:C99)
      SUMIF lowers to a filtered AGGREGATE, which changes cardinality and is not a row expression
  NOW()
      NOW is volatile and has no set-based meaning
  Other!B2
      cross-sheet reference B2: lower the other sheet to a table first
  MIN(B2)
      MIN with one argument is an aggregate in SQL, not a row expression

column E formula = IF(C2>0.05, ROUND(B2*C2,2), 0)
compiled         = CASE WHEN "rate" > 0.05 THEN ROUND("balance" * "rate", 2) ELSE 0 END
rows where compiled SQL agrees with the interpreter: 6/6
xlsx.apply gives 6 values, first = 7875
```

A column formula is one transformation down a column, so it does not need a
cell-by-cell walk. `xlsx.to_sql` lowers it to SQL the database runs once over the
whole table; `xlsx.apply` runs it over an in-memory frame.

The compiler is a second pass over the **same lexer** the evaluator uses, so it
cannot quietly disagree with the interpreter about the dialect.

**The refusals are the feature.** Every refused formula could be lowered to
something plausible, and plausible-and-wrong on every row of a column is the
worst outcome available:

| Refused | Because |
|---|---|
| `VLOOKUP` | lowers to a JOIN |
| `SUMIF` | a filtered aggregate — changes cardinality |
| `NOW()` | volatile; no set-based meaning |
| `Other!B2` | cross-sheet; lower the other sheet to a table first |
| `MIN(B2)` | one argument is SQL's *aggregate* MIN, not the scalar one |

`_row` in the mapping pins the formula's own row, so a reference to a fixed cell
above the data is caught rather than compiled to that row's column.

The last check in that recipe is the one that matters: the compiled SQL is
compared against `xlsx.evaluate` **row by row**. It is the only check that can
catch a compiler which is self-consistently wrong, and it has already earned its
keep — it caught the compiler lowering `AND` while the evaluator still reported
`AND` as unimplemented.

---

## Where to go next

- `docs/xlsx_design.md` — the design, the corpus measurements, and the record of
  what was tried and rejected.
- `tests/run_xlsx.sh` — the main suite, including the tiers that check our output
  with something other than our own reader.
- `docs/reference.md` — the rest of the language.

## Known limits

- Creating a new cell is not supported; only existing cells can be written.
- Dynamic arrays (which spill) and `LET`/`LAMBDA` are not implemented.
- `YEARFRAC` basis 1, `XLOOKUP` non-exact modes and `AGGREGATE` 14–19 are
  refused rather than approximated.
- Against a 15,871-workbook corpus of real Excel files the engine agrees on
  **97.38%** of formula cells, with 91.1% of workbooks showing no disagreement
  at all. `xlsx.check` is how you find out where yours falls.
