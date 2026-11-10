' zip.write's refusals, its same bytes for the same members, and a workbook's
' part put back (gap G55). Self-checking; every answer is written here by
' hand. args[0] = a scratch directory; args[1] = "xlsx" when the build has
' the xlsx module (it needs libxml2, which zip does not).
program main(args)
    dir = args[0]
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

    out = join_path(dir, "refused.zip")
    on error goto next
    zip.write({file}out, [{ name: "a", data: "1" }, { name: "a", data: "2" }])
    checks = check(checks, "two of one name", error.message, "zip.write: two members are named \"a\" (1 and 2), and a reader could not tell which is meant")
    error.clear()
    zip.write({file}out, [{ name: "../a", data: "1" }])
    checks = check(checks, "climbing out", error.message, "zip.write: member 1's name \"../a\" cannot be written: it climbs out with ..")
    error.clear()
    zip.write({file}out, [{ name: "/etc/a", data: "1" }])
    checks = check(checks, "absolute", error.message, "zip.write: member 1's name \"/etc/a\" cannot be written: it is an absolute path")
    error.clear()
    zip.write({file}out, [{ name: "a" + chr(0) + "b", data: "1" }])
    checks = check(checks, "a NUL in a name", contains(error.message, "cannot be written: it holds a NUL"), true)
    error.clear()
    zip.write({file}out, [{ name: "d/", data: "1" }])
    checks = check(checks, "a folder with data", error.message, "zip.write: member \"d/\" is a folder (its name ends in /) and cannot hold data")
    error.clear()
    zip.write({file}out, [{ name: "a" }])
    checks = check(checks, "no data", error.message, "zip.write: member \"a\" has no data (only a folder, a name ending in /, has none)")
    error.clear()
    zip.write({file}out, [{ name: "a", data: "1", modified: "today" }])
    checks = check(checks, "a field it has not", error.message, "zip.write: member 1 has the field 'modified'; a member is { name, data }")
    error.clear()
    zip.write({file}out, [{ name: "a", data: 7 }])
    checks = check(checks, "data not text", error.message, "zip.write: member \"a\"'s data is not text (its bytes)")
    error.clear()
    zip.write({file}out, [{ name: "a", data: "1" }, "b"])
    checks = check(checks, "a member not a record", error.message, "zip.write: member 2 is not a record { name, data }")
    error.clear()
    zip.write({file}out, { name: "a", data: "1" })
    checks = check(checks, "not a list", contains(error.message, "zip.write: the members are a list of { name, data }"), true)
    error.clear()
    zip.write(42, [])
    checks = check(checks, "not a file", contains(error.message, "zip.write expects the file to write"), true)
    error.clear()
    zip.write({file}join_path(join_path(dir, "no_such_dir"), "a.zip"), [{ name: "a", data: "1" }])
    checks = check(checks, "a folder that is not there", contains(error.message, "could not write file: "), true)
    error.clear()
    on error stop
    checks = check(checks, "a refused archive writes nothing", exists({file}out), false)

    ' --- the same members, the same bytes; and none at all -----------------
    m = [{ name: "b.txt", data: repeat("ab", 500) }, { name: "f/" }, { name: "f/c", data: "" }]
    zip.write({file}join_path(dir, "one.zip"), m)
    zip.write({file}join_path(dir, "two.zip"), m)
    checks = check(checks, "same bytes", read({file}join_path(dir, "one.zip")) = read({file}join_path(dir, "two.zip")), true)
    zip.write(join_path(dir, "none.zip"), [])
    checks = check(checks, "an empty archive", count(zip.members({file}join_path(dir, "none.zip"))), 0)
    checks = check(checks, "  is the 22 bytes of its end record", byte_count(read({file}join_path(dir, "none.zip"))), 22)
    ' over a file already there: replaced
    zip.write({file}join_path(dir, "one.zip"), [{ name: "z", data: "z" }])
    checks = check(checks, "written over", zip.read({file}join_path(dir, "one.zip"), "z"), "z")

    ' --- a workbook's part put back -------------------------------------------
    if count(args) < 2 then
        print "checks: " + string(checks.n)
        print "mismatches: " + string(checks.bad)
        return nothing
    end if
    wb = xlsx.new("S")
    xlsx.set(wb, "S", "A1", 41)
    src = join_path(dir, "made.xlsx")
    xlsx.save(wb, src)
    parts = []
    for each p in zip.members({file}src)
        body = zip.read({file}src, p.name)
        if p.name = "xl/worksheets/sheet1.xml" then
            body = replace(body, "<v>41</v>", "<v>42</v>")
        end if
        append(parts, { name: p.name, data: body })
    end for
    back = join_path(dir, "back.xlsx")
    zip.write({file}back, parts)
    b = xlsx.open(back)
    checks = check(checks, "the workbook opens, its edited cell read", b.cell("S", "A1").value, 42)
    names = []
    for each p in zip.members({file}back)
        append(names, p.name)
    end for
    first = []
    for each p in zip.members({file}src)
        append(first, p.name)
    end for
    checks = check(checks, "  its parts in their order", join(names, ","), join(first, ","))

    print "checks: " + string(checks.n)
    print "mismatches: " + string(checks.bad)
end program
