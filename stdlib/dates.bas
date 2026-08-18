' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.

library dates
    function _next_day_name(name)
        if name = "Monday" then
            return "Tuesday"
        end if
        if name = "Tuesday" then
            return "Wednesday"
        end if
        if name = "Wednesday" then
            return "Thursday"
        end if
        if name = "Thursday" then
            return "Friday"
        end if
        if name = "Friday" then
            return "Saturday"
        end if
        if name = "Saturday" then
            return "Sunday"
        end if
        return "Monday"
    end function

    function _previous_day_name(name)
        if name = "Monday" then
            return "Sunday"
        end if
        if name = "Tuesday" then
            return "Monday"
        end if
        if name = "Wednesday" then
            return "Tuesday"
        end if
        if name = "Thursday" then
            return "Wednesday"
        end if
        if name = "Friday" then
            return "Thursday"
        end if
        if name = "Saturday" then
            return "Friday"
        end if
        return "Saturday"
    end function

    function dayname(d)
        cursor(day)= "2026-05-11"
        name = "Monday"

    forward:
        if cursor = d then
            return name
        end if
        if cursor > d then goto backward
        cursor = cursor + 1 day
        name = _next_day_name(name)
        goto forward

    backward:
        if cursor = d then
            return name
        end if
        cursor = cursor - 1 day
        name = _previous_day_name(name)
        goto backward
    end function

    function days_between(a, b)
        start_date(day)= a
        end_date(day)= b
        count = 0

        if start_date = end_date then
            return 0
        end if
        if start_date > end_date then goto backward

    forward:
        if start_date = end_date then
            return count
        end if
        start_date = start_date + 1 day
        count = count + 1
        goto forward

    backward:
        if start_date = end_date then
            return -count
        end if
        start_date = start_date - 1 day
        count = count + 1
        goto backward
    end function

    function _end_of_month(d)
        current(day)= d

    loop:
        candidate = current + 1 day
        if candidate(month)!= current then
            return current
        end if
        current = candidate
        goto loop
    end function

    function _start_of_month(d)
        current(day)= d

    loop:
        candidate = current - 1 day
        if candidate(month)!= current then
            return current
        end if
        current = candidate
        goto loop
    end function

    function _next_weekday(d, target)
        current(day)= d
        current = current + 1 day

    loop:
        current_name = dayname(current)
        if current_name = target then
            return current
        end if
        current = current + 1 day
        goto loop
    end function

    function _previous_weekday(d, target)
        current(day)= d
        current = current - 1 day

    loop:
        current_name = dayname(current)
        if current_name = target then
            return current
        end if
        current = current - 1 day
        goto loop
    end function

    export modifier end of month for assign
        return _end_of_month(value)
    end modifier

    export modifier start of month for assign
        return _start_of_month(value)
    end modifier

    export modifier next monday for assign
        return _next_weekday(value, "Monday")
    end modifier

    export modifier next tuesday for assign
        return _next_weekday(value, "Tuesday")
    end modifier

    export modifier next wednesday for assign
        return _next_weekday(value, "Wednesday")
    end modifier

    export modifier next thursday for assign
        return _next_weekday(value, "Thursday")
    end modifier

    export modifier next friday for assign
        return _next_weekday(value, "Friday")
    end modifier

    export modifier next saturday for assign
        return _next_weekday(value, "Saturday")
    end modifier

    export modifier next sunday for assign
        return _next_weekday(value, "Sunday")
    end modifier

    export modifier previous monday for assign
        return _previous_weekday(value, "Monday")
    end modifier

    export modifier previous tuesday for assign
        return _previous_weekday(value, "Tuesday")
    end modifier

    export modifier previous wednesday for assign
        return _previous_weekday(value, "Wednesday")
    end modifier

    export modifier previous thursday for assign
        return _previous_weekday(value, "Thursday")
    end modifier

    export modifier previous friday for assign
        return _previous_weekday(value, "Friday")
    end modifier

    export modifier previous saturday for assign
        return _previous_weekday(value, "Saturday")
    end modifier

    export modifier previous sunday for assign
        return _previous_weekday(value, "Sunday")
    end modifier

    ' ------------------------------------------------------------------
    ' Business calendars (docs/datetime_design.md §5).
    '
    ' A calendar is DATA -- an ordinary record built by `calendar(spec)` and
    ' passed explicitly to every function, so two teams can hold different
    ' calendars in one program and holidays can come from a file or a database.
    ' gBASIC functions have fixed arity, so the defaults live in the
    ' constructor rather than in optional parameters:
    '
    '     cal = dates.calendar({})                      ' Sat/Sun, no holidays
    '     cal = dates.calendar({ holidays: [x, y] })
    '
    ' Working hours use `open`/`close`, NOT `start`/`end`: `end` is a keyword
    ' that can be a record-literal key but cannot follow a dot, so
    ' `cal.hours.end` would be a parse error -- the same trap consolidate.bas
    ' hit with `as` (see DOGFOOD 2026-08-15).

    function _time_minutes(t)
        parts = split(t, ":")
        return number(parts[0]) * 60 + number(parts[1])
    end function

    function calendar(spec)
        weekend = ["saturday", "sunday"]
        if has(spec, "weekend") then
            weekend = []
            for each w in spec.weekend
                append(weekend, lower(w))
            end for
        end if
        holidays = []
        if has(spec, "holidays") then
            ' Normalised to DAY precision here, once, so membership tests never
            ' hit the precision rule: a holiday supplied as a full timestamp
            ' still blocks the whole day.
            for each h in spec.holidays
                hd (day)= h
                append(holidays, hd)
            end for
        end if
        cal = { weekend: weekend, holidays: holidays }
        if has(spec, "hours") then
            cal.hours = { open: spec.hours.open, close: spec.hours.close }
        end if
        return cal
    end function

    function is_business_day(d, cal)
        dd (day)= d
        if contains(cal.weekend, lower(dd.dayname)) then
            return false
        end if
        if contains(cal.holidays, dd) then
            return false
        end if
        return true
    end function

    function _step_business(d, cal, direction)
        dd (day)= d
        guard = 0
        do
            dd = dd + (1 day) * direction
            guard = guard + 1
            if guard > 3700 then
                ' ~10 years of days. Without this, a calendar with no business
                ' days at all -- which dates.merge can legitimately produce --
                ' turns a lookup into a hang, the least debuggable outcome.
                error "dates: no business day within 10 years; is the calendar empty?"
            end if
        loop until is_business_day(dd, cal)
        return dd
    end function

    function next_business_day(d, cal)
        return _step_business(d, cal, 1)
    end function

    function previous_business_day(d, cal)
        return _step_business(d, cal, 0 - 1)
    end function

    function add_business_days(d, n, cal)
        dd (day)= d
        remaining = n
        while remaining > 0
            dd = next_business_day(dd, cal)
            remaining = remaining - 1
        end while
        while remaining < 0
            dd = previous_business_day(dd, cal)
            remaining = remaining + 1
        end while
        return dd
    end function

    function business_days_between(a, b, cal)
        ' The count of business days d with a < d <= b -- "how many working
        ' days until the deadline" when a is today. Signed: b before a negates.
        ' The convention is stated because half-open intervals are where
        ' calendar bugs live.
        aa (day)= a
        bb (day)= b
        if bb < aa then
            return 0 - business_days_between(b, a, cal)
        end if
        total = 0
        dd = aa
        while dd < bb
            dd = dd + 1 day
            if is_business_day(dd, cal) then
                total = total + 1
            end if
        end while
        return total
    end function

    ' Merging calendars is a UNION OF CONSTRAINTS, which is why finding mutual
    ' meeting days needs no new search machinery: merge, then use any existing
    ' verb. LAW (tested): is_business_day(d, merge([a, b])) equals
    ' is_business_day(d, a) and is_business_day(d, b). Hours intersect --
    ' latest open, earliest close -- and a merge may legitimately produce an
    ' empty window; consumers handle that, merge never raises.
    function merge(cals)
        weekend = []
        holidays = []
        open_txt = unknown
        close_txt = unknown
        for each c in cals
            for each w in c.weekend
                lw = lower(w)
                if not contains(weekend, lw) then
                    append(weekend, lw)
                end if
            end for
            for each h in c.holidays
                if not contains(holidays, h) then
                    append(holidays, h)
                end if
            end for
            if has(c, "hours") then
                if is_unknown(open_txt) then
                    open_txt = c.hours.open
                    close_txt = c.hours.close
                else
                    if _time_minutes(c.hours.open) > _time_minutes(open_txt) then
                        open_txt = c.hours.open
                    end if
                    if _time_minutes(c.hours.close) < _time_minutes(close_txt) then
                        close_txt = c.hours.close
                    end if
                end if
            end if
        end for
        merged = { weekend: weekend, holidays: holidays }
        if not is_unknown(open_txt) then
            merged.hours = { open: open_txt, close: close_txt }
        end if
        return merged
    end function

    ' Calendar difference (docs/datetime_design.md §4.2): "how many months
    ' apart" is a calendar question, deliberately NOT answered by the exact
    ' `datetime - datetime`. Consistent with the accountant's rule by
    ' construction: the month count k is the largest k with a + k months
    ' (CLAMPED, via the core operator) still on or before b -- so
    ' Jan 31 -> Feb 28 is 1 month, exactly as Jan 31 + 1 month is Feb 28.
    function between(a, b, unit)
        aa (day)= a
        bb (day)= b
        if unit = "days" then
            diff = bb - aa
            return diff.total_seconds / 86400
        end if
        if unit = "months" or unit = "years" then
            if bb < aa then
                return 0 - between(b, a, unit)
            end if
            k = (bb.year * 12 + bb.month) - (aa.year * 12 + aa.month)
            if aa + (1 month) * k > bb then
                k = k - 1
            end if
            if unit = "years" then
                return floor(k / 12)
            end if
            return k
        end if
        error "dates.between: unit must be days, months, or years"
    end function
end library
