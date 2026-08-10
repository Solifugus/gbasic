' xlsx -- SHARED FORMULAS: a formula filled down a column, stored once.
'
' Excel writes the text on the anchor cell only:
'     <c r="C2"><f t="shared" ref="C2:C6" si="0">A2*$B$1</f><v>20</v></c>
' and gives every other cell in the run an EMPTY back-reference:
'     <c r="C3"><f t="shared" si="0"/><v>30</v></c>
'
' Read naively, C3 has "a formula whose text is empty", which evaluates to
' #VALUE!. That was not cosmetic: xlsx.recalc writes evaluated values back, so
' it replaced every such cell with #VALUE! -- silent corruption of a real
' workbook, measured at 171 cells on the first corpus file tried.
'
' Nor is it rare. Across the Enron corpus, 61.0% of formula-bearing workbooks
' use shared formulas and 13.2M of the 20.7M formula cells are continuations --
' so nearly two thirds of every formula cell was being read as empty
' (docs/xlsx_design.md §13.J).
'
' Resolving one means TRANSLATING the anchor's text by the offset between the
' cells. Getting that wrong is worse than not doing it, because the result is a
' plausible number computed from the wrong cells -- so each column below
' isolates one rule the translation must obey.

program main(args)
  wb = xlsx.open("examples/fixtures/xlsx/shared.xlsx")

  print "== the continuation reports the formula it stands for =="
  ' What is read back must match what the evaluator computes. The two
  ' disagreeing would be worse than either being wrong alone.
  for each ref in ["C2", "C3", "C6"]
    print "  " + ref + "  " + xlsx.cell(wb, "Filled", ref).formula
  end for

  print ""
  print "== relative shifts, absolute does not =="
  ' C = A{row} * $B$1. The A must follow the row; the $B$1 must not move.
  for each ref in ["C2", "C3", "C4", "C5", "C6"]
    c = xlsx.cell(wb, "Filled", ref)
    print "  " + ref + "  " + c.formula + "   = " + xlsx.evaluate(wb, "Filled", ref) + "   cached " + c.value
  end for

  print ""
  print "== a reference inside a STRING is text, not a reference =="
  ' D = A{row} & "A1". The literal A1 must survive untouched at every row.
  for each ref in ["D2", "D4", "D6"]
    print "  " + ref + "  " + xlsx.cell(wb, "Filled", ref).formula
  end for

  print ""
  print "== mixed anchoring: each half independent =="
  ' E = $A{row} + A$1 -- the column is pinned on the left term, the row on the
  ' right, so exactly one half of each moves.
  for each ref in ["E2", "E4", "E6"]
    print "  " + ref + "  " + xlsx.cell(wb, "Filled", ref).formula
  end for

  print ""
  print "== both endpoints of a RANGE shift =="
  for each ref in ["F2", "F4", "F6"]
    print "  " + ref + "  " + xlsx.cell(wb, "Filled", ref).formula
  end for

  print ""
  print "== the whole sheet agrees with its cached values =="
  ' The real assertion. A translation that shifted by the wrong offset would
  ' still produce plausible numbers, and only this catches that.
  r = xlsx.check(wb, "Filled")
  print "agree    = " + r.agree
  print "disagree = " + r.disagree

  print ""
  print "== recalc leaves them alone rather than overwriting with #VALUE! =="
  ' The corruption case. changed=0 means every recomputed value equalled the
  ' one Excel cached.
  rc = xlsx.recalc(wb, "Filled")
  print "evaluated = " + rc.evaluated + "  changed = " + rc.changed
  print "C6 after  = " + xlsx.cell(wb, "Filled", "C6").value
end program
