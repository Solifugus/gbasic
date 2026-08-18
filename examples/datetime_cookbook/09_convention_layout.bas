' Recipe 9 — Laying out a convention: sessions, gaps, lunch, day rollover.
'
' schedule.layout packs ordered sessions into working days. The rules are
' stated, not discovered: sessions keep their order; one that misses the day
' end moves WHOLE to the next day; breaks are immovable and a bumped session
' resumes exactly at break end; anything that fits on NO day is reported in
' unplaced -- never silently dropped, and never allowed to sink the sessions
' behind it.

program main(args)
    load dates from "../../stdlib/dates.bas"
    load schedule from "../../stdlib/schedule.bas"

    cal = dates.calendar({ hours: { open: "9:00", close: "17:00" } })
    mon (date)= "2026-08-17"
    days = dates.series({ every: "business day" }, { from: mon, count: 3 }, cal)

    plan = {
        gap: 15 minutes,
        breaks: [ { at: "12:00", length: 1 hour, name: "Lunch" } ],
        sessions: [
            { name: "Opening keynote", length: 90 minutes },
            { name: "Workshop A", length: 2 hours },
            { name: "Panel", length: 45 minutes },
            { name: "Deep dive", length: 90 minutes },
            { name: "All-day marathon", length: 10 hours },
            { name: "Closing", length: 1 hour }
        ]
    }

    r = schedule.layout(plan, days, cal)
    for each s in r.scheduled
        print "day " + s.day + "  " + s.starts + " .. " + s.ends + "  " + s.name
    end for
    for each u in r.unplaced
        print "UNPLACED: " + u + " (fits in no working day)"
    end for
end program
