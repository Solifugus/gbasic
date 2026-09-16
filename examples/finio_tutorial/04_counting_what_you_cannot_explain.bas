' Step 4 — Count what you could not explain.
'
' Step 2 grouped issues by code. That is a report about these ten files. §13
' asks for something else: a signal that a FORMAT has moved, which is a claim
' about the world and needs counting across time rather than across one drop.
'
' An adapter that meets a transaction code it holds no direction for is not
' broken. Once is a curiosity. Three hundred times across last quarter is
' stronger evidence that a revision happened than any watch list, and it names
' the field to go and read about.
'
' The rule that makes this safe to keep: an observation records A TOKEN AND A
' LOCATION, never content, and it is enforced. A `detail` longer than 64 bytes
' is refused, because a whole record passed as a "token" is a customer record
' in a log — and that is the one mistake this log could make that would matter.
program main()
    load finio
    load finio_all
    load finio_registry
    load finio_watch

    reg = finio_all.registry()
    found = finio.scan(reg, "tests/finio/foreign/")
    today = "2026-09-16"

    ' The three conformance codes that mean "this adapter did not understand
    ' something", as distinct from "this file's arithmetic is wrong".
    unexplained = [ "unknown_transaction_code", "unsupported_batch_kind", "non_ascii" ]

    log = {}
    for each path in found.files
        if contains(found.unknown, path) then
            continue
        end if
        doc = finio.read_file(reg, path, {})
        for each i in finio.validate(reg, doc)
            if contains(unexplained, i.code) then
                kind = "unknown_code"
                if i.code = "non_ascii" then
                    kind = "unexpected_length"
                end if
                log = finio_watch.observe(log,
                    { adapter: doc.adapter, revision: string(doc.revision),
                      kind: kind,
                      detail: i.code + " " + string(i.found),
                      where: file_name(path),
                      seen: today })
            end if
        end for
    end for

    print "what this adapter could not explain, across the whole drop:"
    for each o in finio_watch.observations_for(log, "aba.nacha")
        print ("  " + o.detail + "   seen " + string(o.occurrences)
               + "x in " + join(o.where, ", "))
    end for
    print ""

    ' The two signals together. §13: "An adapter with old evidence and no
    ' observations may be perfectly healthy — a stable format simply is not
    ' moving. An adapter with recent evidence and a rising observation count is
    ' the interesting case, and only the two together say so."
    entries = finio_registry.all(finio_all.adapters())
    print "the review queue a year from now, ranked:"
    for each q in finio_watch.review_queue(entries, log, "2027-10-01")
        if q.occurrences > 0 then
            print ("  " + q.id + "  rank " + string(q.rank))
            print ("    " + q.why)
        end if
    end for
    print ""
    print "Nothing here modifies an adapter. A check produces WORK, never a"
    print "patch: a process that edited the reader would silently change how a"
    print "file written in 2019 is read, and a source tripwire enforces that."
end program
