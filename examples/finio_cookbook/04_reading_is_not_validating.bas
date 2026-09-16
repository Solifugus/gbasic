' Recipe 4 — Reading and validating are different operations.
'
' §15: invalid input is not necessarily unreadable input.
'
' A NACHA file states its own arithmetic — how many entries it holds, a hash of
' the routing numbers it touched, its debit and credit totals — in control
' records the producer computed. When those disagree with the entries, that is
' the single most operationally important thing an ACH shop can be told. It is
' NOT a reason to refuse to read the file. It is a reason to read it and
' report.
'
' So `read_file` preserves and explains; `validate` judges; and the two are
' never the same call. A library that raised on a control mismatch would hand
' you an exception where you wanted a list of what is wrong.
'
' The severity vocabulary is error / warning / note, and it grades CONFORMANCE
' only — how the source stands against the format. Whether a mismatch is worth
' stopping a payment run for is your judgement, not the library's, so there is
' no "critical" in it.
program main()
    load finio
    load finio_all

    reg = finio_all.registry()

    for each name in [ "payroll.ach", "bad_total.ach" ]
        doc = finio.read_file(reg, "tests/finio/nacha/" + name, {})
        issues = finio.validate(reg, doc)
        print (name)
        print ("  records read:  " + string(count(doc.records)))
        print ("  issues found:  " + string(count(issues)))
        for each i in issues
            where = ""
            if has(i, "record") then
                where = " (record " + string(i.record) + ")"
            end if
            print ("    " + i.severity + " " + i.code + where)
            print ("      " + i.message)
        end for
        print ""
    end for

    ' An adapter with no validator is REFUSED rather than answered with an
    ' empty list. An empty list means "this source conforms" — the strongest
    ' claim available — and returning it for an adapter that never looked would
    ' be indistinguishable from a clean file. Ask first if you need to know.
    a = finio.find_adapter(reg, "aba.nacha")
    print ("aba.nacha declares a validator: " + string(has(a, "validate")))
end program
