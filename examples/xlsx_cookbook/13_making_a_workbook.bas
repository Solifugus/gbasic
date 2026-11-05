' Recipe 13 — Make a workbook from nothing.
'
' xlsx.new() is a workbook with one empty sheet; add_sheet adds more. `set`
' makes a cell that is not there yet, in its place in the row. A typed value
' is written the way a spreadsheet holds it, a number and a number format, so
' a date written reads back as a date, here and in Excel. A formula is written
' with set_formula, and recalc gives it its value.

program main(args)
  wb = xlsx.new("Summary")
  wb.add_sheet("Notes")

  wb.set("Summary", "A1", "Week")
  wb.set("Summary", "B1", "Counted")
  wb.set("Summary", "C1", "Value")
  wb.set("Summary", "A2", {date}"2026-11-13")
  wb.set("Summary", "B2", 412)
  wb.set("Summary", "C2", {USD}"18240.50")
  wb.set("Summary", "A3", {date}"2026-11-20")
  wb.set("Summary", "B3", 398)
  wb.set("Summary", "C3", {USD}"17911.00")
  wb.set("Summary", "D2", 0.031, { format: "0.0%" })   ' any Excel number format, by its code
  wb.set_formula("Summary", "B4", "SUM(B2:B3)")
  wb.set("Notes", "A1", "Counted by the Tuesday team")
  r = wb.recalc()
  print "formulas filled in: " + string(r.changed)

  out = temp_file({ prefix: "summary-" })
  wb.save(string(out))
  back = xlsx.open(string(out))
  for each ref in ["A2", "B4", "C2", "D2"]
    c = back.cell("Summary", ref)
    print ref + "  " + c.kind + "  " + string(c.value) + "  [" + c.format + "]"
  end for
  print "sheets: " + string(back.sheets())
  delete(out)
end program
