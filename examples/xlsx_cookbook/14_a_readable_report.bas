' Recipe 14 — Make a worksheet a reader can use.
'
' A report someone will filter needs headings that look like headings, a
' subtotal shown as the money it adds up, columns wide enough to read, the
' heading row kept in view, and a filter. `set` and `set_formula` take
' { format, bold, fill }, each changing only what it names; column_widths,
' freeze and filter each replace what the sheet had.

program main(args)
  usd = "#,##0.00 \"USD\""
  wb = xlsx.new("Invoices")
  for each h, i in ["Supplier", "Due", "Amount"]
    wb.set("Invoices", ["A1", "B1", "C1"][i], h, { bold: true, fill: "D9D9D9" })
  end for
  rows = [["Harlow Paper", {date}"2026-11-20", {USD}"1840.00"],
          ["Brant & Sons", {date}"2026-11-27", {USD}"312.40"],
          ["Okafor Freight", {date}"2026-12-01", {USD}"5096.75"]]
  for each r, i in rows
    n = string(i + 2)
    wb.set("Invoices", "A" + n, r[0])
    wb.set("Invoices", "B" + n, r[1])
    wb.set("Invoices", "C" + n, r[2])
  end for
  wb.set("Invoices", "B5", "Total", { bold: true })
  ' SUBTOTAL(109, ...) adds only the rows the filter shows
  wb.set_formula("Invoices", "C5", "SUBTOTAL(109,C2:C4)", { format: usd, bold: true })
  wb.column_widths("Invoices", [22, 12, 14])
  wb.freeze("Invoices", 1, 0)
  wb.filter("Invoices", "A1:C4")

  out = temp_file({ prefix: "invoices-" })
  wb.save(string(out))
  back = xlsx.open(string(out))
  for each ref in ["A1", "B2", "C3", "C5"]
    c = back.cell("Invoices", ref)
    print ref + "  " + c.kind + "  [" + c.format + "]"
  end for
  delete(out)
end program
