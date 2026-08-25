' Timezones (docs/datetime_design.md §9): UTC for the timeline, civil time for
' the calendar, zone names at the edges. Three conversion builtins in the
' epoch family -- to_zone, from_zone, zone_offset -- plus zone_resolve, which
' names the DST cases instead of guessing.
'
' Fixed points, hand-checkable against 2026 rules (stable since 2007):
' US DST runs Mar 8 .. Nov 1; EU summer time Mar 29 .. Oct 25. New York is
' UTC-5/-4, Chicago UTC-6/-5, Berlin UTC+1/+2, Kolkata a half-hour zone at
' UTC+5:30 year round.

function check(label, got, want)
    if string(got) = string(want) then
        print "ok " + label
    else
        print "MISMATCH " + label + ": got " + string(got) + " want " + string(want)
    end if
    return 0
end function

program main(args)
    summer {date}= "2026-08-17 12:00:00"
    winter {date}= "2026-01-15 12:00:00"

    ' --- UTC -> zone ---
    x = check("NY in summer (EDT -4) ", to_zone(summer, "America/New_York"), "2026-08-17 08:00:00")
    x = check("NY in winter (EST -5) ", to_zone(winter, "America/New_York"), "2026-01-15 07:00:00")
    x = check("Berlin summer (+2)    ", to_zone(summer, "Europe/Berlin"), "2026-08-17 14:00:00")
    x = check("Kolkata (+5:30)       ", to_zone(summer, "Asia/Kolkata"), "2026-08-17 17:30:00")
    x = check("UTC is identity       ", to_zone(summer, "UTC"), "2026-08-17 12:00:00")

    ' --- zone -> UTC, and the round trip ---
    ny {date}= "2026-08-17 08:00:00"
    x = check("NY local -> UTC       ", from_zone(ny, "America/New_York"), "2026-08-17 12:00:00")
    x = check("round trip            ", to_zone(from_zone(ny, "America/New_York"), "America/New_York"), ny)

    ' --- offsets, as exact durations ---
    x = check("NY offset in August   ", zone_offset(summer, "America/New_York"), "-4 hours")
    x = check("NY offset in January  ", zone_offset(winter, "America/New_York"), "-5 hours")
    x = check("Kolkata half-hour zone", zone_offset(summer, "Asia/Kolkata"), "5 hours 30 minutes")

    ' --- THE FLAGSHIP: a recurring meeting is CIVIL + zone, so each occurrence
    ' converts with ITS OWN offset. "Third Thursday 14:00 Chicago" straddling
    ' the November DST end lands on different UTC instants -- which is exactly
    ' why future intentions must never be stored as UTC.
    oct_mtg {date}= "2026-10-15 14:00:00"
    nov_mtg {date}= "2026-11-19 14:00:00"
    x = check("Oct meeting (CDT -5)  ", from_zone(oct_mtg, "America/Chicago"), "2026-10-15 19:00:00")
    x = check("Nov meeting (CST -6)  ", from_zone(nov_mtg, "America/Chicago"), "2026-11-19 20:00:00")

    ' --- DST edges are NAMED, never guessed ---
    ' Spring forward: 02:30 on 2026-03-08 does not exist in New York.
    gap {date}= "2026-03-08 02:30:00"
    r = zone_resolve(gap, "America/New_York")
    x = check("gap is nonexistent    ", r.kind, "nonexistent")
    x = check("gap shifts FORWARD    ", to_zone(from_zone(gap, "America/New_York"), "America/New_York"), "2026-03-08 03:30:00")

    ' Fall back: 01:30 on 2026-11-01 happens twice in New York.
    twice {date}= "2026-11-01 01:30:00"
    r2 = zone_resolve(twice, "America/New_York")
    x = check("repeat is ambiguous   ", r2.kind, "ambiguous")
    x = check("earlier instant       ", r2.earlier, "2026-11-01 05:30:00")
    x = check("later instant         ", r2.later, "2026-11-01 06:30:00")
    x = check("default = earlier     ", from_zone(twice, "America/New_York"), "2026-11-01 05:30:00")

    ' A normal time resolves unique, and resolve agrees with from_zone.
    r3 = zone_resolve(ny, "America/New_York")
    x = check("normal time is unique ", r3.kind, "unique")
    x = check("resolve = from_zone   ", r3.utc, from_zone(ny, "America/New_York"))

    ' --- precision is preserved through conversion ---
    m {date}= "2026-08-17 08:00"
    x = check("minute in, minute out ", from_zone(m, "America/New_York"), "2026-08-17 12:00")
end program
