' Recipe 12 — SLA clocks: working time that pauses nights, weekends, holidays.
'
' "Respond within 4 business hours" is a deadline computed in WORKING time.
' The clock starts at the next open if the ticket arrives after hours; a
' deadline that exhausts its time exactly at close is due AT close (rolling
' it to next morning would silently extend the SLA); and the whole thing
' round-trips: business_hours_between(a, add_business_hours(a, n, cal)) = n.

program main(args)
    load dates

    cal = dates.calendar({ hours: { open: "9:00", close: "17:00" } })

    ' Four tickets, one promise: respond within 4 business hours.
    t1 {date}= "2026-08-17 09:30:00"
    t2 {date}= "2026-08-17 15:00:00"
    t3 {date}= "2026-08-14 16:00:00"
    t4 {date}= "2026-08-15 11:00:00"
    for each t in [t1, t2, t3, t4]
        due = dates.add_business_hours(t, 4 hours, cal)
        print "in " + t + " (" + t.dayname + ")  ->  due " + due + " (" + due.dayname + ")"
    end for

    ' How much working time did a resolution actually take?
    opened {date}= "2026-08-14 15:00:00"
    closed {date}= "2026-08-17 11:00:00"
    print ""
    print "opened Friday 15:00, closed Monday 11:00 = " + dates.business_hours_between(opened, closed, cal) + " of work time"

    ' The clock respects holidays like every other calendar verb.
    xmas {date}= "2026-12-25"
    hcal = dates.calendar({ holidays: [xmas], hours: { open: "9:00", close: "17:00" } })
    eve {date}= "2026-12-24 15:00:00"
    print "Christmas Eve 15:00 + 4 business hours = " + dates.add_business_hours(eve, 4 hours, hcal)
end program
