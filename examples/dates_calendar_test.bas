' Business calendars (docs/datetime_design.md §5): calendars as data, the
' business-day verbs, dates.merge as a union of constraints, and dates.between.
'
' Fixed points used throughout, cross-checkable by hand: 2026-08-17 is a
' Monday; 2026-12-25 (Christmas) is a Friday; 2026-12-21 is a Monday.

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

    ' --- the default calendar: Sat/Sun weekend, no holidays ---
    cal = dates.calendar({})
    mon {date}= "2026-08-17"
    sat {date}= "2026-08-15"
    x = check("Monday is business    ", dates.is_business_day(mon, cal), true)
    x = check("Saturday is not       ", dates.is_business_day(sat, cal), false)

    ' --- holidays block the day, whatever precision they were supplied at ---
    xmas_stamp {date}= "2026-12-25 09:30:00"
    hcal = dates.calendar({ holidays: [xmas_stamp] })
    xmas {date}= "2026-12-25"
    x = check("holiday normalised    ", dates.is_business_day(xmas, hcal), false)

    ' Christmas 2026 is a FRIDAY: from Thursday the 24th, the next business
    ' day must step over the holiday and the weekend to Monday the 28th.
    thu {date}= "2026-12-24"
    x = check("next skips hol+wkend  ", dates.next_business_day(thu, hcal), "2026-12-28")
    mon28 {date}= "2026-12-28"
    x = check("previous does too     ", dates.previous_business_day(mon28, hcal), "2026-12-24")

    ' --- add_business_days, both directions ---
    fri {date}= "2026-08-14"
    x = check("Fri + 1bd = Mon       ", dates.add_business_days(fri, 1, cal), "2026-08-17")
    x = check("Mon + 5bd = next Mon  ", dates.add_business_days(mon, 5, cal), "2026-08-24")
    x = check("Mon - 1bd = Fri       ", dates.add_business_days(mon, 0 - 1, cal), "2026-08-14")
    x = check("+0 stays put          ", dates.add_business_days(sat, 0, cal), "2026-08-15")

    ' --- business_days_between: count over (a, b], signed ---
    fri21 {date}= "2026-08-21"
    x = check("Mon..Fri = 4          ", dates.business_days_between(mon, fri21, cal), 4)
    mon24 {date}= "2026-08-24"
    x = check("across weekend = 5    ", dates.business_days_between(mon, mon24, cal), 5)
    x = check("signed                ", dates.business_days_between(mon24, mon, cal), 0 - 5)
    x = check("same day = 0          ", dates.business_days_between(mon, mon, cal), 0)

    ' --- merge: THE LAW, checked by arithmetic over two full weeks ---
    ' Alice observes Christmas; Bob does not work Fridays. The merged calendar
    ' must agree with the conjunction on every one of 14 consecutive days --
    ' this is the property that makes "find a mutual meeting day" need no new
    ' machinery.
    alice = dates.calendar({ holidays: [xmas] })
    bob = dates.calendar({ weekend: ["friday", "saturday", "sunday"] })
    both = dates.merge([alice, bob])
    d {date}= "2026-12-21"
    law_holds = true
    for i = 1 to 14
        want = dates.is_business_day(d, alice) and dates.is_business_day(d, bob)
        if dates.is_business_day(d, both) != want then
            law_holds = false
        end if
        d = d + 1 day
    end for
    x = check("merge law over 14 days", law_holds, true)

    ' The merged working window intersects: latest open, earliest close.
    a9 = dates.calendar({ hours: { open: "9:00", close: "17:00" } })
    b10 = dates.calendar({ hours: { open: "10:00", close: "16:30" } })
    hb = dates.merge([a9, b10])
    x = check("merged open           ", hb.hours.open, "10:00")
    x = check("merged close          ", hb.hours.close, "16:30")

    ' A mutual day, end to end: from Wed Dec 23, Alice+Bob's next shared
    ' working day. Thu 24 works for both; prove it against the law's parts.
    wed {date}= "2026-12-23"
    mutual = dates.next_business_day(wed, both)
    x = check("first mutual day      ", mutual, "2026-12-24")
    x = check("mutual works for Alice", dates.is_business_day(mutual, alice), true)
    x = check("mutual works for Bob  ", dates.is_business_day(mutual, bob), true)

    ' --- observed holidays: the day OFF moves to a working day ---
    ' July 4, 2026 is a real Saturday. Under observe: "nearest" the observed
    ' day is Friday July 3 (US federal rule); under "forward" it is Monday
    ' July 6 and the Friday stays a working day.
    july4 {date}= "2026-07-04"
    fri3 {date}= "2026-07-03"
    mon6 {date}= "2026-07-06"
    thu2 {date}= "2026-07-02"
    near = dates.calendar({ holidays: [july4], observe: "nearest" })
    x = check("nearest: Fri observed  ", dates.is_business_day(fri3, near), false)
    x = check("nearest: Mon works     ", dates.is_business_day(mon6, near), true)
    x = check("downstream inherits    ", dates.next_business_day(thu2, near), "2026-07-06")
    fwd = dates.calendar({ holidays: [july4], observe: "forward" })
    x = check("forward: Mon observed  ", dates.is_business_day(mon6, fwd), false)
    x = check("forward: Fri works     ", dates.is_business_day(fri3, fwd), true)
    plain = dates.calendar({ holidays: [july4] })
    x = check("no observe: unchanged  ", dates.is_business_day(fri3, plain), true)

    ' A Sunday holiday observes Monday under both policies; and two weekend
    ' holidays observing forward take CONSECUTIVE weekdays, not one slot.
    sun16 {date}= "2026-08-16"
    nsun = dates.calendar({ holidays: [sun16], observe: "nearest" })
    mon17 {date}= "2026-08-17"
    x = check("Sunday observes Monday ", dates.is_business_day(mon17, nsun), false)
    sat15 {date}= "2026-08-15"
    chain = dates.calendar({ holidays: [sat15, sun16], observe: "forward" })
    tue18 {date}= "2026-08-18"
    x = check("chained: Mon taken     ", dates.is_business_day(mon17, chain), false)
    x = check("chained: Tue taken too ", dates.is_business_day(tue18, chain), false)
    wed19 {date}= "2026-08-19"
    x = check("chained: Wed survives  ", dates.is_business_day(wed19, chain), true)

    ' --- dates.between: the calendar difference, consistent with clamping ---
    jan31 {date}= "2026-01-31"
    feb28 {date}= "2026-02-28"
    x = check("Jan31->Feb28 months   ", dates.between(jan31, feb28, "months"), 1)
    x = check("consistency with +    ", jan31 + 1 month, feb28)
    birth {date}= "1990-06-15"
    ref {date}= "2026-08-17"
    x = check("age in years          ", dates.between(birth, ref, "years"), 36)
    x = check("age in months         ", dates.between(birth, ref, "months"), 434)
    x = check("days                  ", dates.between(mon, mon24, "days"), 7)
    x = check("signed months         ", dates.between(feb28, jan31, "months"), 0 - 1)
end program
