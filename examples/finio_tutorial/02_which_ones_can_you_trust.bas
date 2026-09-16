' Step 2 — Read them all, and find out which ones hold together.
'
' Every one of these ten files READS. That is §15 working: reading and
' validating are different operations, so a file whose control totals disagree
' with its entries still comes back as a document you can look at.
'
' Now ask the second question. A NACHA file states its own arithmetic — entry
' counts, a hash of the routing numbers it touched, debit and credit totals —
' in control records its producer computed. `validate` holds the file to
' numbers it supplied itself.
'
' Two of the ten are completely clean. That is not a disappointing result, it
' is the ordinary one: real files carry returns, reversals, corrections and
' batch types a general reader does not implement, and each of those shows up
' here as something to look at rather than as a silent wrong number.
program main()
    load finio
    load finio_all

    reg = finio_all.registry()
    found = finio.scan(reg, "tests/finio/foreign/")

    clean = 0
    total_issues = 0
    for each path in found.files
        if contains(found.unknown, path) then
            continue
        end if
        doc = finio.read_file(reg, path, {})
        issues = finio.validate(reg, doc)
        total_issues = total_issues + count(issues)
        mark = "  "
        if count(issues) = 0 then
            clean = clean + 1
            mark = "ok"
        end if
        print (mark + "  " + file_name(path)
               + "   " + string(count(doc.records)) + " records"
               + ",  issues " + string(count(issues)))
    end for
    print ""
    print (string(clean) + " of 10 validate completely clean; "
           + string(total_issues) + " issues in all")
    print ""

    ' What kind of thing is wrong? Group by code rather than reading ten
    ' reports: a code repeated across files is a property of the CORPUS, and a
    ' code appearing once is a property of that file.
    by_code = {}
    for each path in found.files
        if contains(found.unknown, path) then
            continue
        end if
        doc = finio.read_file(reg, path, {})
        for each i in finio.validate(reg, doc)
            if has(by_code, i.code) then
                by_code[i.code] = by_code[i.code] + 1
            else
                by_code[i.code] = 1
            end if
        end for
    end for

    print "what kind of thing is wrong:"
    for each c in keys(by_code)
        print ("  " + c + "   " + string(by_code[c]))
    end for
end program
