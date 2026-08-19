' Recipe 11 — Timezones: UTC for the timeline, civil for the calendar, zone
' names at the edges.
'
' The rule below is stored as CIVIL time plus a zone NAME -- never as UTC
' instants. Watch why: the Chicago board meeting is 14:00 local all year, but
' its UTC image SHIFTS an hour when DST ends in November. Stored as 19:00Z,
' the November meetings would silently move. Each occurrence converts with
' its own offset, which is what makes DST arithmetic automatic.

program main(args)
    load dates

    cal = dates.calendar({})
    rule = { every: "month", when: { nth: 3, weekday: "thursday" }, at: "14:00" }
    sep1 (date)= "2026-09-01"
    dec31 (date)= "2026-12-31"

    print "Board meets every 3rd Thursday, 14:00 America/Chicago:"
    for each m in dates.series(rule, { from: sep1, through: dec31 }, cal)
        utc = from_zone(m, "America/Chicago")
        print "  " + m + " local = " + utc + "Z = " + to_zone(utc, "Europe/Berlin") + " Berlin"
    end for

    ' Note the UTC column shifts in November (Chicago leaves DST) while the
    ' Berlin column holds steady -- Europe shifted a week earlier, so the two
    ' zones moved almost together. Only the UTC image jumped. That is the §9
    ' argument in four rows.

    print ""
    off_oct (date)= "2026-10-15 12:00:00"
    off_nov (date)= "2026-11-15 12:00:00"
    print "Chicago offset in Oct: " + zone_offset(off_oct, "America/Chicago")
    print "Chicago offset in Nov: " + zone_offset(off_nov, "America/Chicago")

    ' DST edge cases are NAMED, never guessed. 02:30 on the spring-forward
    ' night does not exist; the fall-back 01:30 happens twice.
    print ""
    gap (date)= "2026-03-08 02:30:00"
    r = zone_resolve(gap, "America/New_York")
    print "2026-03-08 02:30 New York is " + r.kind
    twice (date)= "2026-11-01 01:30:00"
    r2 = zone_resolve(twice, "America/New_York")
    print "2026-11-01 01:30 New York is " + r2.kind + " (earlier " + r2.earlier + "Z, later " + r2.later + "Z)"
end program
