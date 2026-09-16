' Step 1 — What is in this directory?
'
' A night's files land in a drop directory. Some are payment files. Some are
' a licence, a manifest, a README the transfer swept along. Before you can do
' anything you have to know which is which, and the filenames will not tell you
' — the extension is whatever the producer felt like.
'
' `finio.scan` walks a directory, asks each file what it is, and tallies. It
' never raises on one bad file: a file whose bytes cannot be read is its own
' outcome, because an operator deciding what to do about eleven unknown files
' needs to know which of them nobody could even open.
'
' The files here are real ACH files from the moov-io/ach project — written by
' somebody else, which is the only reason this step is worth anything. A
' generator we wrote shares our misunderstandings.
program main()
    load finio
    load finio_all

    reg = finio_all.registry()
    found = finio.scan(reg, "tests/finio/foreign/")

    print ("files examined: " + string(found.examined))
    print ""

    print "recognised:"
    for each k in keys(found.counts)
        print ("  " + k + "   " + string(found.counts[k]) + " files")
    end for
    print ""

    print "not recognised as anything in the registry:"
    for each p in found.unknown
        print ("  " + file_name(p))
    end for
    print ""

    print ("ambiguous (two adapters both claim it): " + string(count(found.ambiguous)))
    print ("unreadable:                             " + string(count(found.unreadable)))
    print ""
    print "The two unknowns are a licence and a provenance manifest. That is the"
    print "right answer: they are not payment files, and a scan that had found a"
    print "format for them would be telling you something false."
end program
