' Recipe 1 — Reading a file without knowing what it is.
'
' The ordinary way to read a financial file is to already know the format and
' call the parser for it. That works right up until the file is not what the
' filename said, which in this industry is often — a `.txt` off an SFTP drop
' could be BAI2, could be a NACHA return, could be a bank's own dialect of
' something.
'
' So `finio` splits the question in two. `identify` reports what the evidence
' says, WITH the evidence. `read_file` then reads it. A file nothing recognises
' is refused, and so is one that two adapters both claim — a plausible reading
' is never chosen silently.
program main()
    load finio
    load finio_all

    reg = finio_all.registry()
    print ("registered: " + join(finio_all.ids(), ", "))
    print ""

    f {file}= "tests/finio/nacha/payroll.ach"
    text = read(f)

    ' What does the evidence say? `exact` is the strongest of finio's four
    ' classifications: exact, strong, possible, unknown.
    id = finio.identify(reg, text)
    print ("classification: " + id.classification)
    for each c in id.candidates
        print ("  " + c.adapter)
        for each why in c.reasons
            print ("    - " + why)
        end for
    end for
    print ""

    ' Now read it. Nothing here names the format.
    doc = finio.read_file(reg, "tests/finio/nacha/payroll.ach", {})
    print ("read as " + doc.adapter + ", " + string(count(doc.records)) + " records")
    print ("the document says WHY it read it that way:")
    for each why in doc.reasons
        print ("  - " + why)
    end for
    print ""

    ' And a file nothing recognises is REFUSED, naming what the registry holds
    ' and how to pin one. Guessing would hand back an ordinary-looking document
    ' read under the wrong layout — every field present, every field wrong.
    on error goto next
    doc = finio.read_file(reg, "tests/finio/nacha/not_ach.txt", {})
    if error then
        print "not_ach.txt:"
        print ("  " + error.message)
        error.clear()
    else
        print "not_ach.txt was read as something (this would be a bug)"
    end if
    on error stop
end program
