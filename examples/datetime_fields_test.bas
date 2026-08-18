' §3 of docs/datetime_design.md: dot fields EXTRACT numbers from datetimes and
' durations; lenses TRUNCATE. Two mechanisms, two jobs — before this, there was
' no way at all to get 2026 out of a datetime as a number short of slicing the
' string, and `d.year` raised "field access expects a record".
'
' The precision rule: reading a field finer than the value's precision yields
' UNKNOWN (a month value has no meaningful day), consistent with unknown as
' absent-information everywhere else. Self-checking throughout.

function check(label, got, want)
    if string(got) = string(want) then
        print "ok " + label
    else
        print "MISMATCH " + label + ": got " + string(got) + " want " + string(want)
    end if
    return 0
end function

program main(args)
    d (date)= "2026-03-15 09:30:45"

    ' --- extraction at full precision ---
    x = check("year        ", d.year, 2026)
    x = check("month       ", d.month, 3)
    x = check("day         ", d.day, 15)
    x = check("hour        ", d.hour, 9)
    x = check("minute      ", d.minute, 30)
    x = check("second      ", d.second, 45)
    x = check("precision   ", d.precision, "second")

    ' Weekday is ISO: Monday=1 .. Sunday=7 (ISO has no zero; Sunday=0 is the
    ' C/JavaScript convention). 2026-03-15 is a Sunday, cross-checked against
    ' dates.dayname which predates this feature.
    x = check("weekday Sun ", d.weekday, 7)
    x = check("dayname     ", d.dayname, "Sunday")
    mon (date)= "2026-03-16"
    x = check("weekday Mon ", mon.weekday, 1)
    x = check("workday test", mon.weekday <= 5, true)

    x = check("day_of_year ", d.day_of_year, 74)
    leap (date)= "2024-12-31"
    x = check("doy leap    ", leap.day_of_year, 366)

    ' Time of day is an EXACT DURATION since midnight (§6): no time kind.
    x = check("time        ", d.time, "9 hours 30 minutes 45 seconds")

    ' --- the precision rule: finer than declared -> unknown ---
    m (month)= d
    x = check("m.year      ", m.year, 2026)
    x = check("m.month     ", m.month, 3)
    x = check("m.day unk   ", is_unknown(m.day), true)
    x = check("m.precision ", m.precision, "month")

    day_only (day)= d
    x = check("d.hour unk  ", is_unknown(day_only.hour), true)
    x = check("d.time unk  ", is_unknown(day_only.time), true)
    x = check("d.weekday ok", day_only.weekday, 7)

    ' --- fields chain through records like any other value ---
    r = { at: d }
    x = check("r.at.year   ", r.at.year, 2026)

    ' --- duration components, as stored (weeks are input sugar) ---
    dur = 1 year 2 months 3 weeks 4 days 5 hours 6 minutes 7 seconds
    x = check("dur.years   ", dur.years, 1)
    x = check("dur.months  ", dur.months, 2)
    x = check("dur.weeks   ", dur.weeks, 3)
    x = check("dur.days    ", dur.days, 4)
    x = check("dur.hours   ", dur.hours, 5)
    x = check("dur.minutes ", dur.minutes, 6)
    x = check("dur.seconds ", dur.seconds, 7)

    t = 1 hour 30 minutes
    x = check("total_secs  ", t.total_seconds, 5400)
    span = 2 days
    x = check("2d total    ", span.total_seconds, 172800)
end program
