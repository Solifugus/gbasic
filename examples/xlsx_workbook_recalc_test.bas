' xlsx -- WORKBOOK-WIDE recalculation (docs/xlsx_design.md §13.M).
'
' xlsx.recalc(wb, sheet) orders ONE sheet. That was correct while the evaluator
' could not see another sheet at all. Once cross-sheet references worked it
' became a trap: a formula on one sheet can depend on a FORMULA on another, and
' recalculating only the first reads the second's stale cached value -- a
' confident wrong number, which is the failure this module exists to avoid.
'
' THE CHAIN in this fixture:
'     Inputs!A1 = 10              a literal
'     Mid!A1    = Inputs!A1 * 2
'     Out!A1    = Mid!A1 + 1
'
' and the sheets are declared in the order Out, Mid, Inputs -- the REVERSE of
' the dependency order. So an engine that recalculated sheets in workbook order
' would hand Out a stale Mid, exactly as evaluating cells top-to-bottom hands
' D7 a stale B5 within one sheet (examples/xlsx_recalc_test.bas).
'
' Set Inputs!A1 to 100 and the only correct answers are Mid=200, Out=201. A
' stale read gives 21, which is why the test names both numbers.

program main(args)
  wb = xlsx.open("examples/fixtures/xlsx/chain.xlsx")

  print "sheet order (reverse of dependency order) = " + join(xlsx.sheets(wb), ", ")
  print "Inputs A1 = " + xlsx.cell(wb, "Inputs", "A1").value
  print "Mid    A1 = " + xlsx.cell(wb, "Mid", "A1").value + "   (Inputs!A1*2)"
  print "Out    A1 = " + xlsx.cell(wb, "Out", "A1").value + "   (Mid!A1+1)"

  xlsx.set(wb, "Inputs", "A1", 100)

  print ""
  print "== recalculating ONE sheet is not enough, and says so by being wrong =="
  ' Out depends on Mid, which has not been recalculated, so Mid still holds the
  ' value cached for Inputs=10. Nothing errors; the number is simply stale.
  r = xlsx.recalc(wb, "Out")
  print "after recalc(wb, \"Out\") : Out A1 = " + xlsx.cell(wb, "Out", "A1").value + "   (stale; correct is 201)"

  print ""
  print "== recalculating the WORKBOOK orders across sheets =="
  w = xlsx.recalc(wb)
  print "evaluated = " + w.evaluated
  print "changed   = " + w.changed
  print "circular  = " + w.circular
  print "Mid A1 = " + xlsx.cell(wb, "Mid", "A1").value + "   (Inputs!A1*2)"
  print "Out A1 = " + xlsx.cell(wb, "Out", "A1").value + "   (transitive: needed the NEW Mid)"
  ' B1 = SUM(Mid!A1:A2) + Inputs!A1 = 200 + 5 + 100, so a range across sheets
  ' participates in the ordering too, not just single cells.
  print "Out B1 = " + xlsx.cell(wb, "Out", "B1").value + "   (SUM(Mid!A1:A2)+Inputs!A1)"

  print ""
  print "== a cycle ACROSS sheets is reported, not iterated =="
  ' Cycle!A1 = Cycle2!B1+1 and Cycle2!B1 = Cycle!A1+1. Neither can be given a
  ' value. Cycle!B1 sits beside them and is healthy: one cycle must not sink
  ' the workbook.
  print "Cycle  B1 (healthy neighbour) = " + xlsx.cell(wb, "Cycle", "B1").value

  print ""
  print "== the new values persist, and the formulas survive =="
  out = "tmp_xlsx_wbrecalc.xlsx"
  xlsx.save(wb, out)
  again = xlsx.open(out)
  print "Out A1 value   = " + xlsx.cell(again, "Out", "A1").value
  print "Out A1 formula = " + xlsx.cell(again, "Out", "A1").formula
  print "Mid A1 value   = " + xlsx.cell(again, "Mid", "A1").value
  print "Mid A1 formula = " + xlsx.cell(again, "Mid", "A1").formula

  f{file} = out
  delete(f)
end program
