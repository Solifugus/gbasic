' Recipe 2 — Where a value came from.
'
' Axiom 2: every interpreted value is traceable to the bytes it was read from.
' Not "the file it came from" — the BYTES. When a payment is wrong at four in
' the afternoon, the question is which characters of which record said so, and
' an answer of "row 137, somewhere" costs an hour.
'
' Each field carries `raw` (exactly the bytes, padding and all) and `value`
' (what they were interpreted as). The LOCATION is not carried — it is asked
' for, with `finio.source_value`, and computed from the retained source.
'
' That is an architecture rather than an omission, and it was measured rather
' than preferred: holding a location per value costs 294x the size of the file
' and is ALSO the slowest to answer, because the working set stops fitting in
' cache. Reconstructing costs about two microseconds a query.
program main()
    load finio
    load finio_all
    load finio_nacha

    reg = finio_all.registry()
    doc = finio.read_file(reg, "tests/finio/nacha/payroll.ach", {})

    r = doc.records[2]
    print ("record " + string(r.index) + " is a " + r.kind
           + ", " + string(r.byte_length) + " bytes at offset " + string(r.byte_offset))
    print ""

    ' The layout is the adapter's; asking for a location means saying which
    ' layout the record was read under.
    lay = finio_nacha.layout_for(r.kind)
    for each concept in [ "individual_name", "amount", "trace_number" ]
        fld = r.fields[concept]
        sv = finio.source_value(doc.source, lay, r.index, concept)
        print (concept + ":")
        print ("  value  " + string(fld.value))
        print ("  raw    [" + fld.raw + "]")
        print ("  where  " + finio.describe_location(sv.location))
    end for
    print ""

    ' The claim is checkable, which is the point of retaining the source: slice
    ' the original text at the offset the location names and the same bytes
    ' come back.
    text = finio.source_text(doc.source)
    sv = finio.source_value(doc.source, lay, r.index, "amount")
    cut = byte_slice(text, sv.location.byte_offset, sv.location.byte_length)
    print ("slicing the whole file at that range gives [" + cut + "]")
    print ("which is the bytes the field reported: " + string(cut = r.fields.amount.raw))
end program
