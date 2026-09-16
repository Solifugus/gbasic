' Recipe 10 — Noticing that a format moved.
'
' An adapter is not finished when it passes its tests. Formats move: ISO 20022
' publishes a new message version every year, a card scheme adds a field, a
' bank changes what it puts in the narrative. §13 says staleness should be
' DERIVABLE rather than remembered.
'
' Two signals, and §13's argument is that neither is much use alone.
'
' A WATCH looks at a published source and reports one of four findings. The
' fourth is the one that matters: `unreachable` is NOT a quiet `unchanged`. A
' source that has been 403-ing for six months is not a stable format, it is a
' watch that stopped working — and this is not hypothetical, since Nacha's own
' developer guide returned 403 to an automated fetch during the first survey.
'
' An OBSERVATION is what production noticed: a code the adapter could not
' explain, a field it did not expect. One is a curiosity; three hundred across
' last quarter is a stronger signal that a revision happened than any watch
' list, and it names the field to go and read about.
'
' Observations record A TOKEN AND A LOCATION, never content, and that is
' ENFORCED — a `detail` longer than 64 bytes is refused, because a whole record
' passed as a "token" is a customer record in a log.
program main()
    load finio
    load finio_all
    load finio_registry
    load finio_watch

    today = "2026-09-16"

    ' --- a watch, checked four ways -----------------------------------------
    print "-- the four findings"
    baseline = finio_watch.check(
        finio_watch.source("document",
            { reference: "https://example.invalid/bai2-guide.pdf",
              watching: "a change to the record-code table" }),
        { ok: true, content: "record codes 01 02 03 16 49 88 98 99" }, today)
    print ("  " + baseline.finding + ": " + baseline.means)

    seen = finio_watch.source("document",
        { reference: "https://example.invalid/bai2-guide.pdf",
          watching: "a change to the record-code table",
          last_seen: baseline.fingerprint })

    same = finio_watch.check(seen, { ok: true, content: "record codes 01 02 03 16 49 88 98 99" }, today)
    print ("  " + same.finding + " (nothing to report)")

    moved = finio_watch.check(seen, { ok: true, content: "record codes 01 02 03 16 49 88 90 98 99" }, today)
    print ("  " + moved.finding + ": " + moved.means)

    gone = finio_watch.check(seen, { ok: false, why: "HTTP 403" }, today)
    print ("  " + gone.finding + " (" + gone.why + "): " + gone.means)
    print ""

    ' --- what production saw ------------------------------------------------
    log = {}
    for each n in [ 1, 2, 3, 4 ]
        log = finio_watch.observe(log,
            { adapter: "aba.nacha", revision: "unresolved",
              kind: "unknown_code", detail: "service class 280",
              where: "batch " + string(n), seen: today })
    end for
    log = finio_watch.observe(log,
        { adapter: "bai2", revision: "2", kind: "unknown_code",
          detail: "type code 921", where: "row 14", seen: today })

    print "-- what production could not explain"
    for each o in finio_watch.observations_for(log, "aba.nacha")
        print ("  " + o.adapter + ": " + o.kind + " " + o.detail
               + ", seen " + string(o.occurrences) + " times")
        print ("    examples: " + join(o.where, ", "))
    end for
    print ""

    ' A detail that is not a token is refused, because it would be content.
    on error goto next
    log = finio_watch.observe(log,
        { adapter: "bai2", revision: "2", kind: "unknown_field",
          detail: "16,409,10000,,,ACME WIDGETS INC PAYROLL 021000021 ACCT 12345678901234/",
          seen: today })
    if error then
        print "-- a whole record offered as a token"
        print ("  refused: " + error.message)
        error.clear()
    end if
    on error stop
    print ""

    ' --- the two together ---------------------------------------------------
    entries = finio_registry.all(finio_all.adapters())
    print "-- the review queue, a year from now"
    queue = finio_watch.review_queue(entries, log, "2027-10-01")
    for each q in queue
        print ("  " + q.id + "  (rank " + string(q.rank) + ", " + q.priority + ")")
        print ("    " + q.why)
    end for
    print ""

    ' A DORMANT FORMAT IS NOT OVERDUE. ISO 8583's last revision is from 2003 and
    ' nobody expects it to move, so it is declared `dormant` and never appears
    ' here. Without that, the queue fills with formats nobody expects to move
    ' and the ones that do are buried — which is how a maintenance list stops
    ' being read.
    listed = []
    for each q in queue
        append(listed, q.id)
    end for
    print ("  formats in the registry: " + string(count(entries)))
    print ("  formats in the queue:    " + string(count(queue)))
    for each e in entries
        if not contains(listed, e.id) then
            print ("  absent: " + e.id + " (" + e.maintenance_priority + ")")
        end if
    end for
end program
