' xlsx — recalculation in dependency order (docs/xlsx_design.md §13.D).
'
' The evaluator could be validated without this, because every input cell
' already carried Excel's cached value. A dependency graph is only needed once
' something CHANGES — which is also what `xlsx.set` was waiting on before it
' could safely touch a formula cell's inputs.
'
' The case that matters is deliberately arranged in the fixture: on `Ledger`,
' B5 = SUM(B2:B3) and D7 = B5*2, and **D7 sits ABOVE B5 in sheet order**. Any
' engine that evaluated cells top to bottom would hand D7 a stale B5 and print
' a confidently wrong number with no error. Order has to be computed.

program main(args)
  wb = xlsx.open("examples/fixtures/xlsx/basic.xlsx")

  print "== before =="
  print "B2 = " + xlsx.cell(wb, "Ledger", "B2").value
  print "B5 = " + xlsx.cell(wb, "Ledger", "B5").value + "   (SUM(B2:B3))"
  print "D7 = " + xlsx.cell(wb, "Ledger", "D7").value + "   (B5*2, and it sits ABOVE B5)"

  print ""
  print "== change an input, then recalculate =="
  xlsx.set(wb, "Ledger", "B2", 1000)
  r = xlsx.recalc(wb, "Ledger")
  print "evaluated=" + r.evaluated + " changed=" + r.changed + " circular=" + r.circular + " unsupported=" + r.unsupported
  print "B2 = " + xlsx.cell(wb, "Ledger", "B2").value
  print "B5 = " + xlsx.cell(wb, "Ledger", "B5").value + "   (1000 + -99.25)"
  print "D7 = " + xlsx.cell(wb, "Ledger", "D7").value + "   (transitive: needed the NEW B5)"

  print ""
  print "== the new values persist, and the formulas do not =="
  ' A recalc that dropped the formulas would look right once and be dead after.
  out = "tmp_xlsx_recalc.xlsx"
  xlsx.save(wb, out)
  again = xlsx.open(out)
  print "B5 value   = " + xlsx.cell(again, "Ledger", "B5").value
  print "B5 formula = " + xlsx.cell(again, "Ledger", "B5").formula
  print "D7 value   = " + xlsx.cell(again, "Ledger", "D7").value
  print "D7 formula = " + xlsx.cell(again, "Ledger", "D7").formula
  print "oracle disagreements after recalc = " + xlsx.check(again, "Ledger").disagree

  print ""
  print "== only the edited sheet's part changed =="
  orig = xlsx.open("examples/fixtures/xlsx/basic.xlsx")
  for each p in xlsx.parts(orig)
    if xlsx.part(orig, p.name) != xlsx.part(again, p.name) then
      print "  changed: " + p.name
    end if
  end for
  print "vendor part still identical = " + (xlsx.part(orig, "xl/customData/vendor.xml") = xlsx.part(again, "xl/customData/vendor.xml"))

  print ""
  print "== a circular reference is reported, not iterated =="
  ' A1 = B1+1 and B1 = A1+1. Excel iterates toward a fixed point only when the
  ' user opts in; the default is to report the cycle. A2/B2 on the same sheet
  ' are healthy and must still evaluate — one bad cycle must not sink the sheet.
  c = xlsx.recalc(wb, "Circular")
  print "evaluated=" + c.evaluated + " changed=" + c.changed + " circular=" + c.circular
  print "the healthy cell on that sheet = " + xlsx.evaluate(wb, "Circular", "B2")

  f{file} = out
  delete(f)
end program
