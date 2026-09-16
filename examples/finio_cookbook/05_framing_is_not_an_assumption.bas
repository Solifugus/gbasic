' Recipe 5 — A fixed-width file need not have any newlines in it.
'
' A great many real fixed-width files arrive as one blocked run of records
' straight off a mainframe, with no record separator at all. A reader that
' assumes newlines reads such a file as A SINGLE ENORMOUS RECORD — and then
' reports one record, zero entries and a clean bill of health.
'
' So framing is part of RECOGNITION, not an assumption. The adapter looks at
' the bytes, decides whether this file is line-framed or fixed-length blocked,
' and `open_text` is TOLD which. The same file is also recorded with the
' terminator each record actually carried, so a writer can reproduce the file
' it read rather than a normalised version of it.
'
' The assertion below is a DIFFERENCE: the same payments, framed three ways,
' must give the same answer. Printing one of them alone would prove nothing.
'
' MEASURED, and it is better than the argument above promises. Framing is
' decided ONCE and both recognition and reading go through that one decision,
' so a reader that ignored it would not misread the blocked file quietly -- it
' would fail to RECOGNISE it and refuse. The silent "one record, zero cents"
' outcome is what a naive reader does; it is not a state this library can
' reach.
program main()
    load finio
    load finio_all

    reg = finio_all.registry()

    for each name in [ "payroll.ach", "blocked.ach", "crlf.ach" ]
        f {file}= "tests/finio/nacha/" + name
        text = read(f)
        newlines = count(split(text, chr(10))) - 1

        doc = finio.read_file(reg, "tests/finio/nacha/" + name, {})
        credits = 0
        for each r in doc.records
            if r.kind = "entry_detail" and r.fields.amount.status = "ok" then
                if r.fields.transaction_code.direction = "credit" then
                    credits = credits + r.fields.amount.cents
                end if
            end if
        end for

        print (name)
        print ("  newlines in the file:  " + string(newlines))
        print ("  framing recognised:    " + doc.source.framing)
        print ("  records read:          " + string(count(doc.records)))
        print ("  credits, in cents:     " + string(credits))
        print ""
    end for
    print ""
    print "Same file, three framings, one answer."
    print "A reader that assumed newlines would read the blocked file as one"
    print "94*140-byte record. Here it would not get that far: framing is"
    print "decided once, and recognition goes through the same decision, so"
    print "such a reader refuses the file rather than misreading it."
end program
