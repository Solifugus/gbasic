' Recipe 3 — Duration algebra: exact and calendar parts, never blurred.
'
' A duration has an exact part (weeks/days/hours/minutes/seconds) and a
' calendar part (years, months). Exact parts have a fixed length; a month
' does NOT. gBASIC keeps the two honest: exact durations order and total,
' month-bearing ones refuse to pretend.

program main(args)
    print "1h + 30m        = " + ((1 hour) + (30 minutes))
    print "2h - 30m        = " + ((2 hours) - (30 minutes))
    print "45m * 4         = " + ((45 minutes) * 4)
    print "1 day / 2       = " + ((1 day) / 2)
    print "signed          = " + ((30 minutes) - (2 hours))

    print ""
    print "90m > 1h        : " + ((90 minutes) > (1 hour))
    print "25h > 1 day     : " + ((25 hours) > (1 day))
    print "90m = 1h30m     : " + ((90 minutes) = (1 hour 30 minutes))
    print "1 week = 7 days : " + ((1 week) = (7 days))
    print "1 month = 30d   : " + ((1 month) = (30 days))

    ' Ordering a month-bearing duration is REFUSED, not guessed -- catch the
    ' refusal to see its message.
    on error resume next
    x = (1 month) > (30 days)
    print ""
    print "refused: " + error.message
    error.clear()

    ' total_seconds works only where a total exists.
    t = 1 hour 30 minutes
    print "total_seconds   = " + t.total_seconds
    mth = 1 month
    on error resume next
    x = mth.total_seconds
    print "refused: " + error.message
    error.clear()
end program
