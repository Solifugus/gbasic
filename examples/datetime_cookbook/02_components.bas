' Recipe 2 — Getting numbers OUT: dot fields extract, lenses truncate.
'
' d.year is a NUMBER you can compute with; {year} gives a coarser DATETIME
' for grouping. Two mechanisms, two jobs, zero global names spent.

program main(args)
    d {date}= "2026-03-15 09:30:45"

    print "year " + d.year + ", month " + d.month + ", day " + d.day
    print "clock " + d.hour + ":" + d.minute + ":" + d.second

    ' Weekday is ISO 8601: Monday=1 .. Sunday=7 (no zero -- Sunday=0 is the
    ' C/JavaScript convention). That makes the workday test one comparison.
    print "weekday " + d.weekday + " (" + d.dayname + ")"
    print "is a workday: " + (d.weekday <= 5)
    print "day of year : " + d.day_of_year

    ' The time of day is an exact DURATION since midnight -- no separate kind.
    print "time of day : " + d.time

    ' THE PRECISION RULE: a field finer than the value's precision is absent
    ' information and reads as unknown -- never a plausible zero.
    m {month}= d
    print ""
    print "month value      : " + m + "  (precision " + m.precision + ")"
    print "its .day is known: " + (not is_unknown(m.day))
end program
