' xlsx Stage 1 — the ZIP container, the part tree, and read-only cells.
' docs/xlsx_design.md §13.A/B/E.
'
' The claim this test exists to hold is "THE READER DISCARDS NOTHING". Round-trip
' write is the destination, and it is only possible if every part survives the
' read — including the ones we do not model. `xlsx.parts` is therefore not a
' convenience: it makes that claim checkable, and the fixture deliberately
' contains a vendor part nothing understands.
'
' The fixture (examples/fixtures/xlsx/basic.xlsx) is built by
' tools/make_xlsx_fixture.py and holds, on purpose: shared strings including an
' XML entity and a non-ASCII word; a sparse sheet (no row 4, no column C); a
' formula cell carrying BOTH its formula and Excel's cached value; a boolean; an
' Excel error; an inline string; and cells styled with money and date number
' formats, so the style index has something to preserve.

program main(args)
  wb = xlsx.open("examples/fixtures/xlsx/basic.xlsx")
  print "handle kind = " + type(wb)
  print "sheets      = " + join(xlsx.sheets(wb), " | ")

  print ""
  print "== the part tree: everything retained =="
  ' `modelled` says whether anything has interpreted the part. A false here is
  ' not a gap — it is a part being carried through untouched, which is exactly
  ' what write needs.
  for each p in xlsx.parts(wb)
    print "  " + p.name + "  bytes=" + p.bytes + "  modelled=" + p.modelled
  end for

  print ""
  print "== a part nothing models survives byte-for-byte =="
  print xlsx.part(wb, "xl/customData/vendor.xml")
  print "absent part = " + is_unknown(xlsx.part(wb, "xl/nope.xml"))

  print ""
  print "== cells are SPARSE: absent, not blank =="
  d = xlsx.dims(wb, "Ledger")
  print "used rows " + d.first_row + "-" + d.last_row + ", cols " + d.first_col + "-" + d.last_col
  print "cell count  = " + count(xlsx.cells(wb, "Ledger"))
  for each c in xlsx.cells(wb, "Ledger")
    f = "-"
    if not is_unknown(c.formula) then
      f = c.formula
    end if
    print "  " + c.ref + "  kind=" + c.kind + "  style=" + c.style + "  formula=" + f + "  value=" + string(c.value)
  end for

  print ""
  print "== a formula cell keeps BOTH halves =="
  ' Excel's own cached value is what makes a recalculation engine testable
  ' later (§13.D), so losing it would cost more than it looks.
  b5 = xlsx.cell(wb, "Ledger", "B5")
  print "formula = " + b5.formula
  print "cached  = " + b5.value

  print ""
  print "== the second sheet, resolved through r:id not by guessing =="
  ' Sheet name cannot imply the part path: Excel numbers parts in creation
  ' order, which need not match tab order.
  for each c in xlsx.cells(wb, "Notes & Refs")
    print "  " + c.ref + " = " + string(c.value)
  end for

  print ""
  print "== handles are reference semantics =="
  other = wb
  print "same handle = " + (other = wb)
end program
