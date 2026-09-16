' Step 3 — The files the specification does not permit, which real producers
' send anyway.
'
' `record_length` was the biggest bucket in step 2, and it is the most
' instructive. NACHA says every record is 94 bytes. Four of these ten files
' have records that are not.
'
' They are not corrupt. Their producer stripped TRAILING BLANKS, so a file
' header arrives at 75 bytes and a file control at exactly 55 — nothing is
' missing except the blank part. A reader with a strict 94-byte rule refuses
' the file outright, which is the wrong answer: the file is perfectly usable
' and the fields that are there are correct.
'
' What is the RIGHT answer? Axiom 7 settles it. A field past the end of a short
' record is one the source said nothing about — `unknown`, not blank, not zero.
' And a field only PARTLY present is `invalid`, never a shorter value: reading
' the first six digits of a truncated ten-digit amount gives a perfectly
' ordinary number a hundred times too small.
'
' One file carries non-ASCII: 94 codepoints in 95 bytes. The specification does
' not permit that either. It is reported and read.
'
' This is the step that could only have been written against files this project
' did not produce. Our own generator emits conforming files, so every fixture
' written here is 94 bytes and none of this was visible.
program main()
    load finio
    load finio_all

    reg = finio_all.registry()
    found = finio.scan(reg, "tests/finio/foreign/")

    print "record lengths actually seen:"
    for each path in found.files
        if contains(found.unknown, path) then
            continue
        end if
        doc = finio.read_file(reg, path, {})
        lens = {}
        for each rec in doc.records
            k = string(rec.byte_length)
            if has(lens, k) then
                lens[k] = lens[k] + 1
            else
                lens[k] = 1
            end if
        end for
        shape = []
        for each k in keys(lens)
            append(shape, k + " x" + string(lens[k]))
        end for
        flag = "   "
        if count(lens) > 1 then
            flag = "-> "
        end if
        print (flag + file_name(path) + "   " + join(shape, ", "))
    end for
    print ""

    ' Look at one short record closely. The file control record here is 55
    ' bytes where the format says 94 — and the fields inside those 55 bytes are
    ' still right.
    doc = finio.read_file(reg, "tests/finio/foreign/ppd-debit.ach", {})
    for each rec in doc.records
        if rec.kind = "file_control" then
            print ("ppd-debit.ach, the " + rec.kind + " record: "
                   + string(rec.byte_length) + " bytes where the format says 94")
            for each concept in keys(rec.fields)
                fld = rec.fields[concept]
                shown = fld.status + "  " + string(fld.value)
                if has(fld, "why") then
                    shown = shown + "  (" + fld.why + ")"
                end if
                print ("  " + concept + ": " + shown)
            end for
        end if
    end for
    print ""
    print "The fields the record actually carries read correctly. The ones past"
    print "its end are `unknown` — the source said nothing — rather than blank"
    print "or zero, which are claims the file never made."
end program
