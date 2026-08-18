' Recipe 7 — Recurring schedules: rules in, dates out.
'
' dates.series takes the same spec vocabulary plus every: (the rhythm) and
' when: (which day inside each period). Bounds are { from:, through: } --
' inclusive, as the name says -- or { from:, count: }. Every emitted day
' satisfies dates.matches with the same rule; the enumerator and the
' predicate verify each other in gBASIC's own test suite.

program main(args)
    load dates from "../../stdlib/dates.bas"

    cal = dates.calendar({})
    jan1 (date)= "2026-01-01"
    jun30 (date)= "2026-06-30"

    ' The board meets every third Thursday at 14:00.
    board = { every: "month", when: { nth: 3, weekday: "thursday" }, at: "14:00" }
    for each m in dates.series(board, { from: jan1, through: jun30 }, cal)
        print "board: " + m
    end for

    ' Payroll every two weeks from an anchor payday, rolled OFF holidays --
    ' backward, so pay is never late. Steps are start + step*k, never
    ' cumulative, so nothing drifts.
    feb13 (date)= "2026-02-13"
    pcal = dates.calendar({ holidays: [feb13] })
    payday1 (date)= "2026-01-02"
    print ""
    for each p in dates.series({ every: 2 weeks, roll: "backward" }, { from: payday1, count: 6 }, pcal)
        print "pay:   " + p
    end for

    ' Standups Monday, Wednesday and Friday: when: WITHOUT nth means EVERY
    ' matching day in the period, not the nth one.
    mon17 (date)= "2026-08-17"
    standup = { every: "week", when: { weekday: ["monday", "wednesday", "friday"] }, at: "9:15" }
    print ""
    for each s in dates.series(standup, { from: mon17, count: 5 }, cal)
        print "standup: " + s
    end for

    ' Month-end billing: multiplicative stepping means Jan 31 -> Feb 28 ->
    ' MAR 31 -- clamping applies per step, and never compounds.
    jan31 (date)= "2026-01-31"
    print ""
    for each b in dates.series({ every: "month" }, { from: jan31, count: 4 }, cal)
        print "bill:  " + b
    end for
end program
