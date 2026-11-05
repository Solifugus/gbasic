' Creating a workbook (gap G14 from the gBASIC books): xlsx.new, add_sheet,
' cells made in place, typed values written as a serial and a number format,
' set_formula and recalc; checked by reading the saved file back. run_xlsx.sh
' then hands the same file to LibreOffice, a reader this engine is not.
' Self-checking.
'
'   gbasic create_test.bas OUT.xlsx FIXTURE_DIR

function check(label, got, want)
    G.checks = G.checks + 1
    if type(got) = type(want) and got = want then
        return nothing
    end if
    G.mismatches = G.mismatches + 1
    print "MISMATCH " + label + ": got " + string(got) + " (" + type(got) + "), want " + string(want) + " (" + type(want) + ")"
    return nothing
end function

' what a refused call said, or ANSWERED
function says(f, a, b, phrase)
    on error goto refused
    r = f(a, b)
    return "ANSWERED"
refused:
    m = error.message
    error.clear()
    return contains(m, phrase)
end function

function try_add(wb, name)
    return wb.add_sheet(name)
end function

function try_set(wb, pair)
    return wb.set("Summary", pair[0], pair[1])
end function

program main(args)
    G = { checks: 0, mismatches: 0 }
    wb = xlsx.new("Summary")
    check("a new workbook has its one sheet", wb.sheets(), ["Summary"])
    wb.add_sheet("Notes & refs")
    check("add_sheet puts it last", wb.sheets(), ["Summary", "Notes & refs"])

    ' cells made out of order land in order
    wb.set("Summary", "C3", {date}"2026-11-05")
    wb.set("Summary", "A1", "Item")
    wb.set("Summary", "B1", "Count")
    wb.set("Summary", "A3", "nuts")
    wb.set("Summary", "A2", "bolts")
    wb.set("Summary", "B2", 12)
    wb.set("Summary", "B3", 30)
    wb.set("Summary", "C2", {datetime}"2026-11-05 13:45:10")
    wb.set("Summary", "D2", 90 minutes)
    wb.set("Summary", "E2", {USD}"1234.5")
    wb.set("Summary", "F2", 0.153, { format: "0.0%" })
    wb.set("Summary", "G2", {time}"06:30:00")
    wb.set("Summary", "H2", 1234.5, { format: "\"$\"#,##0.00" })
    wb.set_formula("Summary", "B4", "SUM(B2:B3)")
    wb.set("Notes & refs", "A1", "made by gBASIC")
    r = wb.recalc()
    check("recalc fills the formula", r.changed, 1)
    wb.save(args[0])

    back = xlsx.open(args[0])
    check("the saved file has both sheets", back.sheets(), ["Summary", "Notes & refs"])
    check("text", back.cell("Summary", "A1").value, "Item")
    check("a number", back.cell("Summary", "B2").value, 12)
    check("a date reads back as the date", back.cell("Summary", "C3").value, {date}"2026-11-05")
    check("  and as kind date", back.cell("Summary", "C3").kind, "date")
    check("a datetime, to the second", back.cell("Summary", "C2").value, {datetime}"2026-11-05 13:45:10")
    check("a duration", back.cell("Summary", "D2").value, 90 minutes)
    check("  and its format", back.cell("Summary", "D2").format, "[h]:mm:ss")
    check("money is its amount", back.cell("Summary", "E2").value, 1234.5)
    check("  with its currency in the format", back.cell("Summary", "E2").format, "#,##0.00 \"USD\"")
    check("an explicit format", back.cell("Summary", "F2").kind, "percent")
    check("a time of day", back.cell("Summary", "G2").value, {time}"06:30:00")
    check("the formula is kept", back.cell("Summary", "B4").formula, "SUM(B2:B3)")
    check("  with recalc's value", back.cell("Summary", "B4").value, 42)
    check("the second sheet's cell", back.cell("Notes & refs", "A1").value, "made by gBASIC")
    d = back.dims("Summary")
    check("the dimension covers every cell", [d.first_row, d.last_row, d.first_col, d.last_col], [1, 4, 0, 7])
    ' Excel requires rows in order and cells in column order within one: the
    ' cells above were written out of order, so read the part's own text
    x = back.part("xl/worksheets/sheet1.xml")
    check("rows are in row order in the part",
          byte_find(x, "<row r=\"1\"") < byte_find(x, "<row r=\"2\"") and
          byte_find(x, "<row r=\"2\"") < byte_find(x, "<row r=\"3\""), true)
    check("cells are in column order in a row (C3 was written before A3)",
          byte_find(x, "r=\"A3\"") < byte_find(x, "r=\"C3\""), true)
    ' a sheet that declares its dimension has it widened to hold a new cell
    f2 = xlsx.open(args[1] + "/formats.xlsx")
    f2.set("Formats", "C30", 1)
    p2 = f2.part("xl/worksheets/sheet1.xml")
    check("the dimension is widened", contains(p2, "<dimension ref=\"A1:C30\"/>"), true)
    ' writing into a sheet read from a file, in a new row between two others
    edited = xlsx.open(args[0])
    edited.set("Summary", "B5", 7)
    check("a cell in a new row of an opened workbook", edited.cell("Summary", "B5").value, 7)

    ' the 1904 system: a date written is the serial that system counts
    old = xlsx.open(args[1] + "/formats1904.xlsx")
    old.set("Formats", "A3", {date}"2023-03-15")
    check("a 1904-system workbook gets its own serial", old.cell("Formats", "A3").serial, 43538)

    ' what is refused
    check("a sheet name twice, ignoring case", says(try_add, wb, "summary", "there is already a sheet Summary"), true)
    check("a sheet name with a slash", says(try_add, wb, "a/b", "cannot hold any of"), true)
    check("a sheet name past 31 characters", says(try_add, wb, repeat("x", 32), "1 to 31 characters"), true)
    check("a date before 1900", says(try_set, wb, ["Z1", {date}"1899-12-31"], "no date before 1900-01-01"), true)
    check("a duration of months", says(try_set, wb, ["Z1", 2 months], "months or years has no length in days"), true)
    check("a value written over a formula", says(try_set, wb, ["B4", 1], "refusing to overwrite a formula cell"), true)
    check("not a cell reference", says(try_set, wb, ["b4", 1], "is not a cell reference"), true)
    print "checks: " + string(G.checks)
    print "mismatches: " + string(G.mismatches)
end program
