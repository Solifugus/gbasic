' Recipe 6 — Date expressions: one spec record, asked three ways.
'
' "Third Thursday", "first Tuesday after the 15th", "last business day before
' the deadline" are all the same shape: constraints on a day, relative to an
' anchor. dates.select finds the one day; dates.matches tests a day;
' dates.series (recipe 7) enumerates. A spec is DATA -- store it in a config
' file, build it in code, print it when something looks wrong.

program main(args)
    load dates from "../../stdlib/dates.bas"

    cal = dates.calendar({})
    d (date)= "2026-08-17"

    print "3rd Thursday      : " + dates.select({ nth: 3, weekday: "thursday", within: "month" }, d, cal)
    print "last Wednesday    : " + dates.select({ nth: "last", weekday: "wednesday", within: "month" }, d, cal)
    print "1st Tue after 15th: " + dates.select({ nth: 1, weekday: "tuesday", after: { day: 15 } }, d, cal)
    print "1st Mon of quarter: " + dates.select({ nth: 1, weekday: "monday", within: "quarter" }, d, cal)

    ' Strictness lives in the NAME -- after excludes the bound, on_or_after
    ' includes it. No more "does next Friday mean this Friday?".
    print "on_or_after Mon   : " + dates.select({ nth: 1, weekday: "monday", on_or_after: d }, d, cal)
    print "after Mon         : " + dates.select({ nth: 1, weekday: "monday", after: d }, d, cal)

    ' A business-day constraint composes with a calendar.
    xmas (date)= "2026-12-25"
    hcal = dates.calendar({ holidays: [xmas] })
    mon28 (date)= "2026-12-28"
    print "last bday before  : " + dates.select({ nth: 1, kind: "business", before: mon28 }, mon28, hcal)

    ' A spec no day satisfies yields UNKNOWN -- a miss, not an error. (A
    ' malformed spec, by contrast, raises.)
    fifth = dates.select({ nth: 5, weekday: "tuesday", within: "month" }, d, cal)
    print "5th Tuesday       : missing = " + is_unknown(fifth)

    ' The same vocabulary as a predicate:
    thu (date)= "2026-08-20"
    print "is 3rd Thursday?  : " + dates.matches(thu, { nth: 3, weekday: "thursday", within: "month" }, cal)
end program
