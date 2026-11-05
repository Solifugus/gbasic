' Cells typed by their number format (gap G13 from the gBASIC books), checked
' against examples/fixtures/xlsx/formats.expected, whose values Python's
' datetime computed from each serial (tools/make_xlsx_formats_fixture.py).
' Both readings are checked: a cell record (wb.cell) and xlsx.grid, which
' grid, consolidate and dbframe read. Self-checking.
'
'   gbasic formats_test.bas FIXTURE_DIR

function check(label, got, want)
    G.checks = G.checks + 1
    if type(got) = type(want) and got = want then
        return nothing
    end if
    G.mismatches = G.mismatches + 1
    print "MISMATCH " + label + ": got " + string(got) + " (" + type(got) + "), want " + string(want) + " (" + type(want) + ")"
    return nothing
end function

' The expected value, as a gBASIC value of the kind the line names.
function want_value(kind, text)
    if kind = "date" then
        return {date}text
    else if kind = "datetime" then
        return {datetime}text
    else if kind = "time" then
        return {time}text
    else if kind = "duration" then
        return number(text) * (1 seconds)
    end if
    return number(text)
end function

' The grid's value and kind at a row of column A.
function grid_at(g, row)
    for each r, i in g.rownos
        if r = row then
            return { value: g.rvals[i][0], kind: g.rkinds[i][0] }
        end if
    end for
    return { value: nothing, kind: "absent" }
end function

program main(args)
    G = { checks: 0, mismatches: 0 }
    f {file}= args[0] + "/formats.expected"
    books = {}
    grids = {}
    for each line in split(trim(read(f)), chr(10))
        p = split(line, " ")
        name = p[0]
        ref = p[1]
        kind = p[2]
        text = p[3]
        if kind = "datetime" then
            text = p[3] + " " + p[4]
        end if
        serial = number(p[count(p) - 1])
        if not has(books, name) then
            books[name] = xlsx.open(args[0] + "/" + name)
            grids[name] = xlsx.grid(books[name], "Formats")
        end if
        c = books[name].cell("Formats", ref)
        label = name + " " + ref
        check(label + " kind", c.kind, kind)
        check(label + " value", c.value, want_value(kind, text))
        check(label + " serial kept", c.serial, serial)
        check(label + " has its format", type(c.format), "string")
        g = grid_at(grids[name], number(mid(ref, 1, len(ref) - 1)))
        check(label + " grid kind", g.kind, kind)
        check(label + " grid value", g.value, want_value(kind, text))
    end for
    ' the codes themselves
    wb = books["formats.xlsx"]
    check("a built-in format's code", wb.cell("Formats", "A1").format, "mm-dd-yy")
    check("  and its id", wb.cell("Formats", "A1").format_id, 14)
    check("a custom format's code", wb.cell("Formats", "A10").format, "yyyy-mm-dd")
    check("  and its id", wb.cell("Formats", "A10").format_id >= 164, true)
    check("General", wb.cell("Formats", "A7").format, "General")
    ' exactness, observed: near 2^60 a double cannot hold +42, an exact integer can (G15)
    big = 1152921504606846976
    check("a whole number is exact", (wb.cell("Formats", "A7").value + big) - big, 42)
    check("  and so are a grid's", (grid_at(grids["formats.xlsx"], 7).value + big) - big, 42)
    check("  and the dimensions", (xlsx.dims(wb, "Formats").last_row + big) - big, 25)
    ' G16: a double past Excel's 15 digits is not a change
    n = xlsx.open(args[0] + "/recalc_noise.xlsx")
    check("recalc with no edit changes nothing", n.recalc().changed, 0)
    n.set("Noise", "B1", 4)
    check("one edit changes the two formulas that read it", n.recalc().changed, 2)
    ' G17: an edited, saved sheet keeps the bytes outside its cells
    before = wb.part("xl/worksheets/sheet1.xml")
    wb.set("Formats", "A7", 43)
    t = temp_file()
    wb.save(string(t))
    after = xlsx.open(string(t)).part("xl/worksheets/sheet1.xml")
    check("the save kept the bytes before the cells, CRLF included",
          byte_slice(before, 0, byte_find(before, "<sheetData")) = byte_slice(after, 0, byte_find(after, "<sheetData")), true)
    check("  and after them, with no newline added",
          byte_slice(before, byte_find(before, "</sheetData>")) = byte_slice(after, byte_find(after, "</sheetData>")), true)
    check("  and the edit is there", xlsx.open(string(t)).cell("Formats", "A7").value, 43)
    delete(t)
    print "checks: " + string(G.checks)
    print "mismatches: " + string(G.mismatches)
end program
