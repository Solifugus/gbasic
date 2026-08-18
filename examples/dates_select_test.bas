' Selectors (docs/datetime_design.md §7): one spec vocabulary, three verbs.
'
' Fixed points, checkable by hand: 2026-08-17 is a Monday, so August 2026 has
' Thursdays on 6/13/20/27, Wednesdays on 5/12/19/26, Tuesdays on 4/11/18/25.
' 2026-01-01 is a Thursday; Christmas 2026 is a Friday; 2026-10-31 is a
' Saturday. The series checks are PROPERTIES -- every emitted day must satisfy
' dates.matches with the same rule -- so the two verbs verify each other.

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
    cal = dates.calendar({})
    aug (date)= "2026-08-17"

    ' --- select, scoped: nth within the anchor's period ---
    x = check("3rd Thursday          ", dates.select({ nth: 3, weekday: "thursday", within: "month" }, aug, cal), "2026-08-20")
    x = check("last Wednesday        ", dates.select({ nth: "last", weekday: "wednesday", within: "month" }, aug, cal), "2026-08-26")
    x = check("nth -1 = last         ", dates.select({ nth: 0 - 1, weekday: "wednesday", within: "month" }, aug, cal), "2026-08-26")
    x = check("5th Tuesday -> unknown", is_unknown(dates.select({ nth: 5, weekday: "tuesday", within: "month" }, aug, cal)), true)
    x = check("1st Monday of quarter ", dates.select({ nth: 1, weekday: "monday", within: "quarter" }, aug, cal), "2026-07-06")
    x = check("1st Friday of year    ", dates.select({ nth: 1, weekday: "friday", within: "year" }, aug, cal), "2026-01-02")
    wed (date)= "2026-08-19"
    x = check("last bday of ISO week ", dates.select({ nth: 0 - 1, kind: "business", within: "week" }, wed, cal), "2026-08-21")

    ' --- select, anchored: strictness is in the name ---
    x = check("1st Tue after the 15th", dates.select({ nth: 1, weekday: "tuesday", after: { day: 15 } }, aug, cal), "2026-08-18")
    xmas (date)= "2026-12-25"
    hcal = dates.calendar({ holidays: [xmas] })
    mon28 (date)= "2026-12-28"
    x = check("1st bday before a date", dates.select({ nth: 1, kind: "business", before: mon28 }, mon28, hcal), "2026-12-24")
    x = check("on_or_after includes  ", dates.select({ nth: 1, weekday: "monday", on_or_after: aug }, aug, cal), "2026-08-17")
    x = check("after excludes        ", dates.select({ weekday: "monday", after: aug }, aug, cal), "2026-08-24")
    x = check("bare spec = next-after", dates.select({ weekday: "friday" }, aug, cal), "2026-08-21")

    ' --- roll conventions, on the month-end payment date ---
    oct (date)= "2026-10-15"
    x = check("roll modified stays in month", dates.select({ nth: 1, day: 31, within: "month", roll: "modified" }, oct, cal), "2026-10-30")
    x = check("roll forward crosses  ", dates.select({ nth: 1, day: 31, within: "month", roll: "forward" }, oct, cal), "2026-11-02")

    ' --- matches: the same vocabulary as a predicate ---
    thu20 (date)= "2026-08-20"
    thu13 (date)= "2026-08-13"
    sat (date)= "2026-08-15"
    rule = { nth: 3, weekday: "thursday", within: "month" }
    x = check("matches 3rd Thursday  ", dates.matches(thu20, rule, cal), true)
    x = check("2nd Thursday does not ", dates.matches(thu13, rule, cal), false)
    x = check("matches kind business ", dates.matches(sat, { kind: "business" }, cal), false)

    ' --- series, period mode: board meets every 3rd Thursday at 14:00 ---
    jan1 (date)= "2026-01-01"
    jun30 (date)= "2026-06-30"
    board = { every: "month", when: { nth: 3, weekday: "thursday" }, at: "14:00" }
    meetings = dates.series(board, { from: jan1, through: jun30 }, cal)
    x = check("6 meetings Jan-Jun    ", count(meetings), 6)
    x = check("first stamped 14:00   ", meetings[0], "2026-01-15 14:00:00")
    all_good = true
    for each m in meetings
        if not dates.matches(m, { nth: 3, weekday: "thursday", within: "month" }, cal) then
            all_good = false
        end if
    end for
    x = check("every one matches rule", all_good, true)

    ' except: removes the March meeting, leaving a gap -- not a reschedule.
    mar19 (date)= "2026-03-19"
    xrule = { every: "month", when: { nth: 3, weekday: "thursday" }, except: [mar19] }
    trimmed = dates.series(xrule, { from: jan1, through: jun30 }, cal)
    x = check("except leaves a gap   ", count(trimmed), 5)

    ' --- series, stepping mode: payroll every 2 weeks, rolled off holidays ---
    feb13 (date)= "2026-02-13"
    pcal = dates.calendar({ holidays: [feb13] })
    payday1 (date)= "2026-01-02"
    paydays = dates.series({ every: 2 weeks, roll: "backward" }, { from: payday1, count: 6 }, pcal)
    x = check("26 biweekly? 6 asked  ", count(paydays), 6)
    x = check("holiday payday rolled ", paydays[3], "2026-02-12")
    x = check("others unmoved        ", paydays[4], "2026-02-27")
    clean = true
    for each p in paydays
        if not dates.is_business_day(p, pcal) then
            clean = false
        end if
    end for
    x = check("no payday on off-day  ", clean, true)

    ' Monthly stepping is MULTIPLICATIVE from the start, so month-end does not
    ' drift: Jan 31 -> Feb 28 -> MAR 31, not Feb-28-forever.
    jan31 (date)= "2026-01-31"
    ends = dates.series({ every: "month" }, { from: jan31, count: 4 }, cal)
    x = check("no cumulative drift   ", string(ends[2]), "2026-03-31")
    x = check("clamps where it must  ", string(ends[1]), "2026-02-28")

    ' Business-day stepping over a holiday and a weekend.
    wed23 (date)= "2026-12-23"
    run = dates.series({ every: "business day" }, { from: wed23, count: 3 }, hcal)
    x = check("bday steps skip hol   ", string(run[2]), "2026-12-28")
end program
