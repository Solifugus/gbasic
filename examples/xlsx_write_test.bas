' xlsx Stage 2 — write and round-trip (docs/xlsx_design.md §13.A).
'
' The whole reason the reader keeps a part tree is this test. `libxlsxwriter`
' was rejected because it generates new workbooks and cannot edit an existing
' one; the claim replacing it is that an UNTOUCHED PART SURVIVES BYTE-FOR-BYTE.
' Everything below exists to hold that claim to account:
'
'   * save with no edits at all -> every part identical on re-read
'   * save after editing ONE sheet -> that part changes and NOTHING ELSE does,
'     including xl/customData/vendor.xml, which nothing in the module models
'   * a formula cell keeps its formula through the trip
'
' Writing over a formula is REFUSED rather than guessed at: the cached value and
' the formula would disagree, Excel would recalculate on open, and the edit
' would silently revert. Until the recalc engine exists that is a wrong answer
' waiting to happen, so it raises.

function part_diff(a_wb, b_wb)
  changed = []
  for each p in xlsx.parts(a_wb)
    x = xlsx.part(a_wb, p.name)
    y = xlsx.part(b_wb, p.name)
    if x != y then
      append(changed, p.name)
    end if
  end for
  return changed
end function

program main(args)
  src = "examples/fixtures/xlsx/basic.xlsx"
  copy_path = "tmp_xlsx_roundtrip.xlsx"
  edit_path = "tmp_xlsx_edited.xlsx"

  print "== save with no edits: a pure round trip =="
  wb = xlsx.open(src)
  n = xlsx.save(wb, copy_path)
  print "wrote bytes > 0 = " + (n > 0)
  back = xlsx.open(copy_path)
  print "part count      = " + count(xlsx.parts(back))
  print "parts changed   = " + count(part_diff(wb, back))

  print ""
  print "== edit one sheet: only that part may change =="
  wb2 = xlsx.open(src)
  print "B2 before = " + xlsx.cell(wb2, "Ledger", "B2").value
  xlsx.set(wb2, "Ledger", "B2", 9999.99)
  xlsx.set(wb2, "Ledger", "A1", "Renamed")
  xlsx.set(wb2, "Ledger", "B6", false)
  xlsx.save(wb2, edit_path)

  edited = xlsx.open(edit_path)
  print "B2 after  = " + xlsx.cell(edited, "Ledger", "B2").value
  print "A1 after  = " + xlsx.cell(edited, "Ledger", "A1").value
  print "B6 after  = " + xlsx.cell(edited, "Ledger", "B6").value
  print "B5 formula survives = " + xlsx.cell(edited, "Ledger", "B5").formula
  print "B5 cached survives  = " + xlsx.cell(edited, "Ledger", "B5").value

  original = xlsx.open(src)
  print ""
  print "changed vs original:"
  for each nm in part_diff(original, edited)
    print "  " + nm
  end for
  print "(everything else, including the unmodelled vendor part, is untouched)"
  print "vendor part identical = " + (xlsx.part(original, "xl/customData/vendor.xml") = xlsx.part(edited, "xl/customData/vendor.xml"))

  print ""
  print "== writing over a formula is refused, not guessed =="
  ' The refusal itself is asserted by the negative tier in tests/run_xlsx.sh,
  ' where the message is the contract. What is checked HERE is the property that
  ' outlives it: the formula cell is still a formula cell after everything else.
  ' (Catching the raise inline is possible since PLAT-ERR, but a golden that
  ' swallowed the refusal would stop pinning its wording.)
  print "B5 still has a formula = " + (not is_unknown(xlsx.cell(edited, "Ledger", "B5").formula))

  c(file) = copy_path
  delete(c)
  e(file) = edit_path
  delete(e)
end program
