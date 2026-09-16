' Step 5 — Changing one field and sending the file back.
'
' The last thing an operations team does with a file is edit one field in it
' and return it. A discretionary-data field is wrong; a name was transcribed
' badly; a trace number needs correcting.
'
' This is where §17's byte fidelity earns its place. `finio_nacha.write_doc` is
' RE-EMISSION, NOT ORIGINATION: it writes back the bytes it read, record by
' record, with the terminator each record actually carried. A file that arrived
' blocked goes back blocked; one with CRLF goes back with CRLF. Nothing is
' normalised, because "normalised" means "different from what the counterparty
' sent", and the counterparty's system is the one that has to read it.
'
' `set_field` REFUSES rather than truncates. A fixed-width field is exactly as
' wide as it is, so a value of the wrong length is a mistake to report — never
' something to pad or cut silently, because a cut account number is still a
' plausible account number.
program main()
    load finio
    load finio_all
    load finio_nacha

    reg = finio_all.registry()
    path = "tests/finio/nacha/payroll.ach"
    f {file}= path
    original = read(f)

    doc = finio.read_file(reg, path, {})

    ' First: read it and write it straight back out, untouched.
    same = finio_nacha.write_doc(doc)
    print ("read and re-emitted unchanged: byte-identical to the file that "
           + "arrived: " + string(same.text = original))
    print ""

    ' Now change one 22-byte name field. Exactly 22 bytes, padded by us.
    before = doc.records[2].fields.individual_name
    print ("record 2 individual_name before: [" + before.raw + "]")

    fixed = finio_nacha.set_field(doc, 2, "individual_name",
                                  "ALICE MERCER-OKONJO   ")
    after = fixed.records[2].fields.individual_name
    print ("record 2 individual_name after:  [" + after.raw + "]")
    print ("                         value:  " + string(after.value))
    print ""

    out = finio_nacha.write_doc(fixed)
    print ("the emitted file is the same length: "
           + string(byte_count(out.text) = byte_count(original)))
    print ("and differs from the original:       "
           + string(out.text != original))

    ' Only that record moved. Everything before and after it is untouched —
    ' which is the property an operations team actually needs, because a
    ' counterparty diffing the two files must see one line change.
    changed = 0
    i = 0
    while i < count(doc.records)
        if doc.records[i].raw != fixed.records[i].raw then
            changed = changed + 1
        end if
        i = i + 1
    end while
    print ("records that changed:                " + string(changed))
    print ""

    ' And a value of the wrong width is refused, not padded.
    on error goto next
    bad = finio_nacha.set_field(doc, 2, "individual_name", "ALICE MERCER")
    if error then
        print ("a 12-byte value for a 22-byte field:")
        print ("  " + error.message)
        error.clear()
    end if
    on error stop
end program
