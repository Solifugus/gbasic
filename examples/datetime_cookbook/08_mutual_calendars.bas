' Recipe 8 — Finding a day that works for everyone.
'
' Merging calendars is a UNION OF CONSTRAINTS -- more weekend, more holidays,
' a narrower hours window -- so a merged calendar plugs into every verb that
' takes one. Finding a mutual meeting day needs no new machinery, and the
' guarantee is a law: a day is a business day in the merge exactly when it is
' one in EVERY constituent.

program main(args)
    load dates from "../../stdlib/dates.bas"

    xmas (date)= "2026-12-25"
    alice = dates.calendar({ holidays: [xmas], hours: { open: "9:00", close: "17:00" } })
    bob = dates.calendar({ weekend: ["friday", "saturday", "sunday"], hours: { open: "10:00", close: "16:30" } })

    both = dates.merge([alice, bob])
    print "merged weekend : " + both.weekend
    print "merged window  : " + both.hours.open + " to " + both.hours.close

    ' First day they can both meet, starting from Wed Dec 23.
    wed (date)= "2026-12-23"
    day = dates.next_business_day(wed, both)
    print "mutual day     : " + day
    print "works for Alice: " + dates.is_business_day(day, alice)
    print "works for Bob  : " + dates.is_business_day(day, bob)

    ' Their whole mutual week, as a series over the merged calendar.
    print ""
    for each d in dates.series({ every: "business day" }, { from: wed, count: 4 }, both)
        print "slot day: " + d + " (" + d.dayname + ")"
    end for
end program
