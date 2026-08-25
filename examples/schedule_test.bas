' schedule.bas (docs/datetime_design.md §8): the convention scenario and the
' physician-slots scenario, checked by ARITHMETIC per §11 -- every placed
' session's span must equal its declared length, nothing may overlap the
' lunch break or leave the working window, order must survive day rollover,
' and the slot grid must tile the day exactly.

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
    load schedule from "../stdlib/schedule.bas"

    cal = dates.calendar({ hours: { open: "9:00", close: "17:00" } })
    mon {date}= "2026-08-17"

    ' --- the convention: 6 sessions, one impossible, into business days ---
    plan = {
        gap: 15 minutes,
        breaks: [ { at: "12:00", length: 1 hour, name: "Lunch" } ],
        sessions: [
            { name: "Opening", length: 90 minutes },
            { name: "Workshop", length: 2 hours },
            { name: "Panel", length: 45 minutes },
            { name: "QA", length: 90 minutes },
            { name: "Monster", length: 10 hours },
            { name: "Closing", length: 1 hour }
        ]
    }
    days = dates.series({ every: "business day" }, { from: mon, count: 3 }, cal)
    r = schedule.layout(plan, days, cal)

    x = check("5 placed              ", count(r.scheduled), 5)
    x = check("1 unplaced            ", count(r.unplaced), 1)
    x = check("unplaced by name      ", r.unplaced[0], "Monster")
    x = check("opening at open       ", r.scheduled[0].starts, "2026-08-17 09:00:00")
    x = check("workshop bumped past lunch", r.scheduled[1].starts, "2026-08-17 13:00:00")
    x = check("QA rolls to day 2     ", r.scheduled[3].day, 2)
    x = check("QA at day-2 open      ", r.scheduled[3].starts, "2026-08-18 09:00:00")
    x = check("order survives rollover", r.scheduled[4].name, "Closing")
    x = check("closing after QA + gap", r.scheduled[4].starts, "2026-08-18 10:45:00")

    ' Arithmetic properties, not transcripts:
    spans_ok = true
    inside_ok = true
    lunch_ok = true
    for each s in r.scheduled
        want_len = 0 seconds
        for each p in plan.sessions
            if p.name = s.name then
                want_len = p.length
            end if
        end for
        if (s.ends - s.starts) != want_len then
            spans_ok = false
        end if
        sday {day}= s.starts
        if s.starts < dates.at(sday, "9:00") or s.ends > dates.at(sday, "17:00") then
            inside_ok = false
        end if
        lunch_s = dates.at(sday, "12:00")
        lunch_e = dates.at(sday, "13:00")
        if s.starts < lunch_e and s.ends > lunch_s then
            lunch_ok = false
        end if
    end for
    x = check("every span = its length", spans_ok, true)
    x = check("everything inside hours", inside_ok, true)
    x = check("nothing overlaps lunch ", lunch_ok, true)

    ' --- physician slots: 20-minute appointments, 10-minute gaps, lunch ---
    grid = schedule.slots(mon, { length: 20 minutes, gap: 10 minutes, breaks: [ { at: "12:00", length: 1 hour } ] }, cal)
    ' Morning 9:00-12:00 tiles 6 slots on the 30-minute cycle; afternoon
    ' 13:00-17:00 tiles 8 (the last ends 16:50). Hand-checkable: 14.
    x = check("14 slots in the day   ", count(grid), 14)
    x = check("first at open         ", grid[0].starts, "2026-08-17 09:00:00")
    x = check("afternoon resumes 13:00", grid[6].starts, "2026-08-17 13:00:00")
    x = check("last ends before close", grid[13].ends, "2026-08-17 16:50:00")
    tile_ok = true
    for each sl in grid
        if (sl.ends - sl.starts) != (20 minutes) then
            tile_ok = false
        end if
    end for
    x = check("every slot exactly 20m", tile_ok, true)
end program
