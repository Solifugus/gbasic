' Recipe 10 — An appointment book: slice the day into bookable slots.
'
' schedule.slots turns one working day into a grid -- the physician pattern:
' fixed-length appointments, a cleanup gap between them, lunch excluded.
' WHICH slots are taken is the application's state, not the library's; a
' booking system stores slot starts against patients and filters this grid.

program main(args)
    load dates from "../../stdlib/dates.bas"
    load schedule from "../../stdlib/schedule.bas"

    cal = dates.calendar({ hours: { open: "9:00", close: "17:00" } })
    mon {date}= "2026-08-17"

    grid = schedule.slots(mon, { length: 20 minutes, gap: 10 minutes, breaks: [ { at: "12:00", length: 1 hour } ] }, cal)

    print count(grid) + " bookable slots on " + mon + ":"
    for each sl in grid
        print "  " + sl.starts + " .. " + sl.ends
    end for
end program
