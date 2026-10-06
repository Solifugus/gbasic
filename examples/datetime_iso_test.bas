' ISO 8601's `T` SEPARATOR AND ZONE DESIGNATOR.
'
' `2026-03-07T14:05:09Z` is what a web API hands you and it was REFUSED: the
' scanner wanted a space at position 10 and nothing after the seconds. Both are
' syntax rather than semantics, so they are normalised away before the one
' strict scanner sees them, which keeps it the single place that decides what a
' datetime looks like.
'
' AN OFFSET IS HONOURED BY CONVERTING TO UTC, and the type forces that rather
' than taste: a gBASIC datetime is CIVIL and carries no zone, while
' `14:05:09+02:00` denotes an INSTANT. Turning an instant into a civil time
' needs a zone and UTC is the only one the text implies. Keeping the wall clock
' and dropping the offset would make `14:05:09+02:00` and `14:05:09Z` the SAME
' VALUE while they are two hours apart -- a wrong answer with nothing raised.
'
' SELF-CHECKING, because every defect here is a PLAUSIBLE TIMESTAMP: an offset
' applied the wrong way round is off by twice the offset and still reads like a
' date, and a golden would record it.

function check(label, got, want)
    if string(got) = string(want) then
        print "ok   " + label
    else
        print "MISMATCH " + label + ": got " + string(got) + ", want " + string(want)
    end if
    return nothing
end function

program main( args )
    print "-- the T separator is a separator and nothing more"
    a {datetime}= "2026-03-07T14:05:09"
    check("T parses            ", a, "2026-03-07 14:05:09")
    b {datetime}= "2026-03-07 14:05:09"
    check("and agrees with space", b, a)

    print ""
    print "-- Z is UTC, so the civil fields already ARE the answer"
    c {datetime}= "2026-03-07T14:05:09Z"
    check("Z is a no-op        ", c, "2026-03-07 14:05:09")
    d {datetime}= "2026-03-07 14:05:09Z"
    check("with a space too    ", d, "2026-03-07 14:05:09")
    e {datetime}= "2026-03-07T14:05:09+00:00"
    check("+00:00 likewise     ", e, "2026-03-07 14:05:09")

    print ""
    print "-- a non-zero offset converts to UTC, and the SIGN is the whole point"
    f {datetime}= "2026-03-07T14:05:09+02:00"
    check("+02:00 goes BACK    ", f, "2026-03-07 12:05:09")
    g {datetime}= "2026-03-07T14:05:09-05:00"
    check("-05:00 goes FORWARD ", g, "2026-03-07 19:05:09")

    print ""
    print "-- the compact spellings of the same offset agree"
    h {datetime}= "2026-03-07T14:05:09+0200"
    check("+0200               ", h, f)
    i {datetime}= "2026-03-07T14:05:09+02"
    check("+02                 ", i, f)

    print ""
    print "-- IT IS DATE ARITHMETIC, NOT HOUR SUBTRACTION"
    ' The check an off-by-a-field implementation fails: the offset crosses
    ' midnight, the month and the year at once.
    j {datetime}= "2026-01-01T00:30:00+02:00"
    check("crosses a year      ", j, "2025-12-31 22:30:00")
    k {datetime}= "2026-03-01T00:30:00+02:00"
    check("and a month         ", k, "2026-02-28 22:30:00")

    print ""
    print "-- lower precisions keep theirs"
    l {datetime}= "2026-03-07T14:05Z"
    check("minute precision    ", l, "2026-03-07 14:05:00")
    m {datetime}= "2026-03-07T14Z"
    check("hour precision      ", m, "2026-03-07 14:00:00")

    print ""
    print "-- CONTROLS: a bare date ends in `-07` and is NOT an offset"
    ' The reason the suffix is looked for only AFTER the date/time separator. A
    ' scan from the right would read `2026-03-07` as seven hours west.
    n {datetime}= "2026-03-07"
    check("a bare date         ", n, "2026-03-07 00:00:00")
    o {datetime}= "2026-03"
    check("month precision     ", o, "2026-03-01 00:00:00")
    p {datetime}= "2026"
    check("year precision      ", p, "2026-01-01 00:00:00")

    print ""
    print "-- CONTROLS: and the refusals, so this is not `accept anything`"
    on error goto next
    q {datetime}= "2026-03-07Z"
    check("a zone with no time ", contains(error.message, "ISO-like"), true)
    error.clear()
    r {datetime}= "2026-03-07T14:05:09+99:00"
    check("an impossible offset", contains(error.message, "ISO-like"), true)
    error.clear()
    s2 {datetime}= "2026-03-07T14:05:09+"
    check("a bare sign         ", contains(error.message, "ISO-like"), true)
    error.clear()
    t2 {datetime}= "2026-03-07T14:05:09x"
    check("trailing junk       ", contains(error.message, "ISO-like"), true)
    error.clear()
    u2 {datetime}= "2026-03-07X14:05:09"
    check("some other letter   ", contains(error.message, "ISO-like"), true)
    error.clear()
end program
