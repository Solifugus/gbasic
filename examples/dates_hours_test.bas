' Business-hours arithmetic (docs/datetime_design.md §9): working time that
' pauses overnight, across weekends and holidays.
'
' The rules under test, as decided: a clock starting outside working hours
' starts at the next open (backward: previous close); a deadline exhausting
' its time exactly at close lands AT close (rolling to next morning would
' silently EXTEND an SLA); the window is half-open (open counts, close does
' not); durations must be exact; negative durations walk backward. The
' load-bearing check is the ROUND-TRIP LAW:
'     business_hours_between(a, add_business_hours(a, n, cal), cal) = n

function check(label, got, want)
    if string(got) = string(want) then
        print "ok " + label
    else
        print "MISMATCH " + label + ": got " + string(got) + " want " + string(want)
    end if
    return 0
end function

program main(args)
    load dates from "../stdlib/dates.bas"
    cal = dates.calendar({ hours: { open: "9:00", close: "17:00" } })

    mon13 {date}= "2026-08-17 13:00:00"
    mon15 {date}= "2026-08-17 15:00:00"
    mon09 {date}= "2026-08-17 09:00:00"
    mon10 {date}= "2026-08-17 10:00:00"

    ' --- the SLA shape ---
    x = check("4h from 13:00 = close  ", dates.add_business_hours(mon13, 4 hours, cal), "2026-08-17 17:00:00")
    x = check("4h from 15:00 spans    ", dates.add_business_hours(mon15, 4 hours, cal), "2026-08-18 11:00:00")
    x = check("8h = one full day      ", dates.add_business_hours(mon09, 8 hours, cal), "2026-08-17 17:00:00")
    x = check("8h from 10:00          ", dates.add_business_hours(mon10, 8 hours, cal), "2026-08-18 10:00:00")

    ' --- the clock starts inside working hours ---
    early {date}= "2026-08-17 06:00:00"
    x = check("early start -> open+1h ", dates.add_business_hours(early, 1 hour, cal), "2026-08-17 10:00:00")
    sat {date}= "2026-08-15 11:00:00"
    x = check("weekend start -> Monday", dates.add_business_hours(sat, 1 hour, cal), "2026-08-17 10:00:00")
    x = check("zero clamps to open    ", dates.add_business_hours(sat, 0 hours, cal), "2026-08-17 09:00:00")

    ' --- weekends and holidays pause the clock ---
    fri16 {date}= "2026-08-14 16:00:00"
    x = check("Fri 16:00 + 4h = Mon 12", dates.add_business_hours(fri16, 4 hours, cal), "2026-08-17 12:00:00")
    xmas {date}= "2026-12-25"
    hcal = dates.calendar({ holidays: [xmas], hours: { open: "9:00", close: "17:00" } })
    eve {date}= "2026-12-24 15:00:00"
    x = check("holiday+weekend paused ", dates.add_business_hours(eve, 4 hours, hcal), "2026-12-28 11:00:00")

    ' --- negative durations walk backward ---
    x = check("Mon 10:00 - 2h = Fri 16", dates.add_business_hours(mon10, (0 hours) - (2 hours), cal), "2026-08-14 16:00:00")

    ' --- elapsed working time ---
    tue11 {date}= "2026-08-18 11:00:00"
    x = check("13->17 same day        ", dates.business_hours_between(mon13, mon13 + 4 hours, cal), "4 hours")
    x = check("15:00 -> Tue 11:00     ", dates.business_hours_between(mon15, tue11, cal), "4 hours")
    mon11 {date}= "2026-08-17 11:00:00"
    fri15 {date}= "2026-08-14 15:00:00"
    x = check("across the weekend     ", dates.business_hours_between(fri15, mon11, cal), "4 hours")
    x = check("same instant = zero    ", dates.business_hours_between(mon13, mon13, cal), "0 seconds")
    x = check("signed when reversed   ", dates.business_hours_between(mon11, fri15, cal), "-4 hours")
    satstart {date}= "2026-08-15 08:00:00"
    x = check("weekend start counts 0 ", dates.business_hours_between(satstart, mon13, cal), "4 hours")

    ' --- THE ROUND-TRIP LAW, over mixed durations ---
    a {date}= "2026-08-17 10:30:00"
    law_ok = true
    for each n in [1 hour, 4 hours, 90 minutes, 20 hours]
        got = dates.business_hours_between(a, dates.add_business_hours(a, n, cal), cal)
        if got != n then
            law_ok = false
        end if
    end for
    x = check("between(a, add(a,n))=n ", law_ok, true)

    ' --- the working-instant predicate: open counts, close does not ---
    x = check("10:00 is working time  ", dates.is_business_time(mon10, cal), true)
    x = check("open instant counts    ", dates.is_business_time(mon09, cal), true)
    at_close {date}= "2026-08-17 17:00:00"
    x = check("close instant does not ", dates.is_business_time(at_close, cal), false)
    x = check("Saturday does not      ", dates.is_business_time(sat, cal), false)
end program
