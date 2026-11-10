' Making a worksheet readable (gap G55 from the gBASIC books): bold, a fill
' and a number format on set and set_formula, each changing only what it
' names; column widths, frozen panes and a filter, each replacing what was
' set before. Self-checking against this engine's own reading of the saved
' file; run_xlsx.sh then hands the same file to LibreOffice
' (tests/xlsx_formats/style_oracle.py holds what LibreOffice must show).
'
'   gbasic style_test.bas OUT.xlsx FIXTURE_DIR
program main(args)
    checks = { n: 0, bad: 0 }

    function check(checks, label, got, want)
        checks.n = checks.n + 1
        if type(got) = type(want) then
            if got = want then
                return checks
            end if
        end if
        checks.bad = checks.bad + 1
        print "MISMATCH " + label + ": got " + type(got) + " " + string(got) + ", want " + type(want) + " " + string(want)
        return checks
    end function

    usd = "#,##0.00 \"USD\""
    s = "Q'3 Report"
    wb = xlsx.new(s)
    xlsx.add_sheet(wb, "Empty")
    for each h, i in ["Name", "Amount", "Share"]
        xlsx.set(wb, s, ["A1", "B1", "C1"][i], h, { bold: true, fill: "D9D9D9" })
    end for
    xlsx.set(wb, s, "A2", "north")
    xlsx.set(wb, s, "A3", "south")
    xlsx.set(wb, s, "A4", "east")
    xlsx.set(wb, s, "B2", {USD}"1250.5")
    ' bold first, then the value: the money format is added to the bold
    xlsx.set(wb, s, "B3", 0, { bold: true })
    xlsx.set(wb, s, "B3", {USD}"300.25")
    xlsx.set(wb, s, "B4", {USD}"75")
    ' the format first, then bold: the format is kept
    xlsx.set(wb, s, "C2", 0.5, { format: "0.0%" })
    xlsx.set(wb, s, "C2", 0.5, { bold: true })
    ' a fill, then none
    xlsx.set(wb, s, "C3", 0.3, { fill: "fff2cc" })
    xlsx.set(wb, s, "C3", 0.3, { fill: "none" })
    xlsx.set(wb, s, "A5", "Total", { bold: true })
    wb.set_formula(s, "B5", "SUBTOTAL(109,B2:B4)", { format: usd, bold: true })
    ' fifty bold cells share one style
    for i = 10 to 59
        xlsx.set(wb, s, "D" + string(i), i, { bold: true })
    end for
    ' each replaces what was set before
    xlsx.column_widths(wb, s, [30])
    xlsx.column_widths(wb, s, [18, 14, 10])
    xlsx.freeze(wb, s, 1, 1)
    wb.freeze(s, 1, 0)
    xlsx.filter(wb, s, "A1:C3")
    xlsx.filter(wb, s, "A1:C4")
    ' and each comes off
    xlsx.column_widths(wb, "Empty", [5])
    xlsx.column_widths(wb, "Empty", [])
    xlsx.freeze(wb, "Empty", 2, 0)
    xlsx.freeze(wb, "Empty", 0, 0)
    xlsx.filter(wb, "Empty", "A1:B3")
    wb.filter("Empty", "")
    xlsx.set(wb, "Empty", "A1", 1)
    xlsx.save(wb, args[0])

    b = xlsx.open(args[0])
    checks = check(checks, "a heading's value", b.cell(s, "A1").value, "Name")
    checks = check(checks, "money kept its format with bold", b.cell(s, "B3").format, usd)
    checks = check(checks, "  and its amount", b.cell(s, "B3").value, 300.25)
    checks = check(checks, "a percent kept its format when made bold", b.cell(s, "C2").format, "0.0%")
    checks = check(checks, "a formula's format", b.cell(s, "B5").format, usd)
    checks = check(checks, "  its formula", b.cell(s, "B5").formula, "SUBTOTAL(109,B2:B4)")
    checks = check(checks, "a plain cell stays General", b.cell(s, "A2").format, "General")
    checks = check(checks, "the other sheet's cell", b.cell("Empty", "A1").value, 1)

    ' --- refused, by name ---------------------------------------------------
    on error goto next
    xlsx.set(wb, s, "A1", "x", { bolt: true })
    checks = check(checks, "a misspelt option", error.message, "xlsx.set: unknown option 'bolt' (known: format, bold, fill)")
    error.clear()
    xlsx.set(wb, s, "A1", "x", { bold: "yes" })
    checks = check(checks, "bold not a boolean", error.message, "xlsx.set: bold is true or false")
    error.clear()
    xlsx.set_formula(wb, s, "B5", "SUM(B2:B4)", { fill: "grey" })
    checks = check(checks, "a fill not a colour", error.message, "xlsx.set_formula: fill is a colour as six hex digits, red green blue, such as \"D9D9D9\" (a light grey), or \"none\"")
    error.clear()
    xlsx.set(wb, s, "A1", "x", { format: "" })
    checks = check(checks, "an empty format", error.message, "xlsx.set: format is an Excel number format code, such as \"#,##0.00\"")
    error.clear()
    xlsx.set(wb, s, "A1", "x", "bold")
    checks = check(checks, "options not a record", contains(error.message, "xlsx.set: the options are a record"), true)
    error.clear()
    xlsx.column_widths(wb, s, [10, 0])
    checks = check(checks, "a width of 0", error.message, "xlsx.column_widths: width 2 is not a number of characters from above 0 to 255, the most a column holds")
    error.clear()
    xlsx.column_widths(wb, s, 10)
    checks = check(checks, "widths not a list", contains(error.message, "the widths are a list of numbers"), true)
    error.clear()
    xlsx.freeze(wb, s, 1.5, 0)
    checks = check(checks, "half a row", error.message, "xlsx.freeze: the rows to keep in view are a whole number from 0 to 1048575")
    error.clear()
    xlsx.freeze(wb, s, 1, -1)
    checks = check(checks, "no columns below 0", error.message, "xlsx.freeze: the columns to keep in view are a whole number from 0 to 16383")
    error.clear()
    xlsx.filter(wb, s, "A1")
    checks = check(checks, "a filter over one cell", contains(error.message, "the range is two cells"), true)
    error.clear()
    xlsx.filter(wb, s, "C4:A1")
    checks = check(checks, "a range backwards", error.message, "xlsx.filter: C4:A1 does not run from its top-left cell to its bottom-right")
    error.clear()
    xlsx.filter(wb, s, "A1:C1")
    checks = check(checks, "a heading alone", error.message, "xlsx.filter: A1:C1 is one row, its heading, and filters no rows")
    error.clear()
    xlsx.freeze(wb, "Nope", 1, 0)
    checks = check(checks, "no such sheet", error.message, "xlsx: no such sheet: Nope")
    error.clear()
    on error stop

    ' --- a workbook another program wrote ------------------------------------
    f = xlsx.open(join_path(args[1], "basic.xlsx"))
    sh = f.sheets()[0]
    before = f.cell(sh, "A1")
    f.set(sh, "A1", before.value, { bold: true })
    f.freeze(sh, 1, 0)
    out = replace(args[0], ".xlsx", "_basic.xlsx")
    f.save(out)
    g = xlsx.open(out)
    checks = check(checks, "its cell's value stands", g.cell(sh, "A1").value, before.value)
    checks = check(checks, "  and its format", g.cell(sh, "A1").format, before.format)

    print "checks: " + string(checks.n)
    print "mismatches: " + string(checks.bad)
end program
