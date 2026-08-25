' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.

' schedule.bas — packing events into working days (docs/datetime_design.md §8).
'
' Its own library rather than more of dates.bas, because packing is not date
' arithmetic: dates answers WHICH day, schedule answers WHERE IN THE DAY things
' fit. Two entry points:
'
'   schedule.slots(day, spec, cal)    — cut one day into appointment slots
'   schedule.layout(plan, days, cal)  — pack ordered sessions into days
'
' Decided rather than discovered, and tested: sessions keep their order; a
' session that does not fit before day end MOVES WHOLE to the next day (never
' split silently); a session that fits on NO day lands in `unplaced:` rather
' than being dropped; breaks are immovable and gaps are not inserted around
' them twice. Booking state (who took which slot) is the application's, not
' this library's.

library schedule
    load dates from "dates.bas"

    function _hours_or_error(cal)
        if not has(cal, "hours") then
            error "schedule: the calendar has no hours: { open:, close: } window"
        end if
        return cal.hours
    end function

    ' Resolve a plan's breaks onto one concrete day: [{ bs:, be: }].
    ' Breaks are listed in day order by the caller (documented, not sorted).
    function _day_breaks(dd, holder)
        out = []
        if not has(holder, "breaks") then
            return out
        end if
        for each b in holder.breaks
            bs = dates.at(dd, b.at)
            append(out, { bs: bs, be: bs + b.length })
        end for
        return out
    end function

    ' Move a start time forward past every break it would collide with. A
    ' bumped session restarts EXACTLY at break end -- the gap belongs between
    ' sessions, not around breaks.
    function _bump(cur, length, dbs)
        c = cur
        moved = true
        guard = 0
        while moved and guard < 50
            moved = false
            guard = guard + 1
            for each b in dbs
                if c < b.be and c + length > b.bs then
                    c = b.be
                    moved = true
                end if
            end for
        end while
        return c
    end function

    function slots(day, spec, cal)
        hours = _hours_or_error(cal)
        dd {day}= day
        cur = dates.at(dd, hours.open)
        closing = dates.at(dd, hours.close)
        gap = 0 seconds
        if has(spec, "gap") then
            gap = spec.gap
        end if
        dbs = _day_breaks(dd, spec)
        out = []
        guard = 0
        while guard < 2000
            guard = guard + 1
            st = _bump(cur, spec.length, dbs)
            en = st + spec.length
            if en > closing then
                return out
            end if
            append(out, { starts: st, ends: en })
            cur = en + gap
        end while
        return out
    end function

    function layout(plan, days, cal)
        hours = _hours_or_error(cal)
        gap = 0 seconds
        if has(plan, "gap") then
            gap = plan.gap
        end if
        sessions = plan.sessions
        scheduled = []
        unplaced = []
        si = 0
        for di = 1 to count(days)
            if si >= count(sessions) then
                break
            end if
            dd {day}= days[di - 1]
            opening = dates.at(dd, hours.open)
            closing = dates.at(dd, hours.close)
            dbs = _day_breaks(dd, plan)
            cur = opening
            while si < count(sessions)
                s = sessions[si]
                st = _bump(cur, s.length, dbs)
                en = st + s.length
                if en <= closing then
                    append(scheduled, { name: s.name, starts: st, ends: en, day: di })
                    cur = en + gap
                    si = si + 1
                else
                    ' Would it fit on a FRESH day? If not, no day will ever
                    ' hold it -- report it and keep filling this one, so one
                    ' oversized session does not sink the schedule behind it.
                    st0 = _bump(opening, s.length, dbs)
                    if st0 + s.length > closing then
                        append(unplaced, s.name)
                        si = si + 1
                    else
                        break
                    end if
                end if
            end while
        end for
        while si < count(sessions)
            append(unplaced, sessions[si].name)
            si = si + 1
        end while
        return { scheduled: scheduled, unplaced: unplaced }
    end function
end library
