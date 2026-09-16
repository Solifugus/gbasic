' Recipe 7 — What the reader did NOT claim.
'
' Axiom 8: loss is explicit. §18: what the layout did not claim is preserved as
' unknown rather than dropped. Local interpretation usually lives exactly
' there — the field your bank uses for a cost centre, the record type a vendor
' added, the trailing bytes nobody documented.
'
' Two mechanisms, and they catch different things.
'
' `coverage` is arithmetic over a fixed-width layout: the fields account for
' some of the record and the rest is a GAP. An adapter that quietly forgot a
' field shows up here — as a gap, rather than as nothing at all. An OVERLAP is
' the other half, and it is what transcribing a published guide produces, since
' those number positions from 1 and a layout copied straight out is off by one
' everywhere.
'
' `doc.loss` is the reader's own report of bytes it did not interpret. The
' bytes are KEPT either way — a loss note says what was not claimed, it does
' not throw anything away.
program main()
    load finio
    load finio_all
    load finio_nacha

    reg = finio_all.registry()

    print "-- coverage: does each layout account for all 94 bytes?"
    for each kind in [ "file_header", "entry_detail", "batch_control" ]
        cov = finio.coverage(finio_nacha.layout_for(kind), 94)
        print ("  " + kind + ": complete " + string(cov.complete)
               + ", gaps " + string(count(cov.gaps))
               + ", overlaps " + string(count(cov.overlaps)))
    end for
    print ""

    print "-- and a layout with a field removed reports the hole it left"
    full = finio_nacha.layout_for("entry_detail")
    short = []
    for each spec in full
        if spec.concept != "individual_name" then
            append(short, spec)
        end if
    end for
    cov = finio.coverage(finio.layout(short), 94)
    print ("  complete " + string(cov.complete) + ", gaps " + string(count(cov.gaps)))
    for each g in cov.gaps
        print ("  gap at byte " + string(g.offset) + " for " + string(g.length) + " bytes")
    end for
    print ""

    ' A record type this adapter does not interpret. Turn the addenda record
    ' (type 7) into a type 4, which NACHA does not define and finio_nacha
    ' therefore does not claim.
    f {file}= "tests/finio/nacha/payroll.ach"
    text = read(f)
    odd = replace(text, "705PAYROLL", "405PAYROLL")
    doc = finio.read_text(reg, odd, {})

    print "-- a record type the adapter does not interpret"
    print ("  records read: " + string(count(doc.records)) + "   (nothing was dropped)")
    print ("  loss notes:   " + string(count(doc.loss)))
    for each l in doc.loss
        print ("    " + l.kind + ", record " + string(l.record)
               + ", bytes " + string(l.byte_offset) + " + " + string(l.byte_length))
        print ("    " + l.why)
    end for
end program
