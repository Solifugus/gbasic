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
        if has(spec, "observe") then
            ' Observed holidays: when a holiday lands on the weekend, the day
            ' OFF moves to a working day. observe: "nearest" picks the closest
            ' free weekday (ties break forward) -- the US federal rule: a
            ' Saturday holiday is observed Friday, a Sunday one Monday, and it
            ' generalises to any weekend shape. observe: "forward" always
            ' shifts to the next free weekday (the UK substitute-day style).
            ' Chains resolve: two weekend holidays observing forward take
            ' consecutive weekdays rather than colliding. The original day
            ' stays in the list (it is already non-working via the weekend);
            ' the observed day is ADDED, computed once, at construction --
            ' so every downstream verb inherits it with no further logic.
            if spec.observe != "nearest" and spec.observe != "forward" then
                error "dates: observe must be nearest or forward"
            end if
            observed = []
            for each h in holidays
                append(observed, h)
            end for
            for each h in holidays
                if contains(weekend, lower(h.dayname)) then
                    pick = h
                    found_slot = false
                    dist = 1
                    while not found_slot and dist <= 30
                        fwd = h + (1 day) * dist
                        if not contains(weekend, lower(fwd.dayname)) and not contains(observed, fwd) then
                            pick = fwd
                            found_slot = true
                        end if
                        if not found_slot and spec.observe = "nearest" then
                            back = h - (1 day) * dist
                            if not contains(weekend, lower(back.dayname)) and not contains(observed, back) then
                                pick = back
                                found_slot = true
                            end if
                        end if
                        dist = dist + 1
                    end while
                    if found_slot then
                        append(observed, pick)
                    end if
                end if
            end for
            cal.holidays = observed
        end if
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

    ' ------------------------------------------------------------------
    ' Selectors (docs/datetime_design.md §7): ONE spec vocabulary, three
    ' verbs. matches(d, spec, cal) asks; select(spec, anchor, cal) finds the
    ' one day (or unknown); series(spec, bounds, cal) enumerates.
    '
    ' Two field names differ from the design's first draft, both for the same
    ' grammar reason as hours.open/close: a keyword can be a record-literal
    ' key but cannot follow a dot. The series sub-rule is `when:` (not `on:`,
    ' since spec.on is a parse error) and bounds are `{ from:, through: }`
    ' (not `to:` -- bounds.to is a parse error; `through` is also honest about
    ' being INCLUSIVE). A malformed spec ERRORS; a spec no day satisfies
    ' yields UNKNOWN -- the two failure modes mean different things.

    function _pad2(n)
        if n < 10 then
            return "0" + n
        end if
        return "" + n
    end function

    function _mkday(y, m, d)
        s = y + "-" + _pad2(m) + "-" + _pad2(d)
        out (date)= s
        return out
    end function

    ' Scope bounds lean on the core operators rather than a day table: the
    ' last day of a month is first-of-month + 1 month - 1 day, which is
    ' correct in February because the core add clamps.
    function _scope(d, within)
        if within = "month" then
            first = _mkday(d.year, d.month, 1)
            return { first: first, last: first + 1 month - 1 day }
        end if
        if within = "quarter" then
            qm = floor((d.month - 1) / 3) * 3 + 1
            first = _mkday(d.year, qm, 1)
            return { first: first, last: first + 3 months - 1 day }
        end if
        if within = "year" then
            return { first: _mkday(d.year, 1, 1), last: _mkday(d.year, 12, 31) }
        end if
        if within = "week" then
            dd (day)= d
            first = dd - (1 day) * (dd.weekday - 1)   ' ISO week: Monday first
            return { first: first, last: first + 6 days }
        end if
        error "dates: within must be week, month, quarter, or year"
    end function

    function _tod(t)
        if type(t) = "duration" then
            return t
        end if
        parts = split(t, ":")
        secs = number(parts[0]) * 3600 + number(parts[1]) * 60
        if count(parts) > 2 then
            secs = secs + number(parts[2])
        end if
        return (1 second) * secs
    end function

    ' A day plus a time of day. t is a string ("14:00") or an exact duration.
    ' Note "0:00" adds a zero duration, which does not bump precision -- a
    ' midnight stamp keeps day precision, which renders without a time.
    function at(d, t)
        dd (day)= d
        return dd + _tod(t)
    end function

    function _nth_num(v)
        if type(v) = "string" then
            return 0 - 1                              ' "last"
        end if
        return v
    end function

    function _wd_ok(d, w)
        name = lower(d.dayname)
        if type(w) = "array" then
            for each x in w
                if lower(x) = name then
                    return true
                end if
            end for
            return false
        end if
        return lower(w) = name
    end function

    function _excepted(dd, spec)
        if not has(spec, "except") then
            return false
        end if
        for each e in spec.except
            ed (day)= e
            if ed = dd then
                return true
            end if
        end for
        return false
    end function

    ' The non-positional constraints: is this day even eligible?
    function _candidate(dd, spec, cal)
        if has(spec, "weekday") then
            if not _wd_ok(dd, spec.weekday) then
                return false
            end if
        end if
        if has(spec, "day") then
            if dd.day != spec.day then
                return false
            end if
        end if
        if has(spec, "month") then
            ' A number, or a list: { day: 15, month: [1, 7] } is "the 15th of
            ' January and July" (RRULE's BYMONTH).
            if type(spec.month) = "array" then
                if not contains(spec.month, dd.month) then
                    return false
                end if
            else
                if dd.month != spec.month then
                    return false
                end if
            end if
        end if
        if has(spec, "kind") then
            if spec.kind = "business" then
                if not is_business_day(dd, cal) then
                    return false
                end if
            end if
        end if
        if _excepted(dd, spec) then
            return false
        end if
        return true
    end function

    ' An anchor bound is a datetime, or a component record like { day: 15 }
    ' meaning "day 15 of the reference day's month".
    function _bound(b, ref)
        if type(b) = "record" then
            return _mkday(ref.year, ref.month, b.day)
        end if
        bb (day)= b
        return bb
    end function

    function _apply_roll(dd, spec, cal)
        if not has(spec, "roll") then
            return dd
        end if
        if is_business_day(dd, cal) then
            return dd
        end if
        if spec.roll = "forward" then
            return next_business_day(dd, cal)
        end if
        if spec.roll = "backward" then
            return previous_business_day(dd, cal)
        end if
        if spec.roll = "modified" then
            ' Forward, unless that crosses into the next month -- then back.
            ' The finance convention, where a payment date must stay in its
            ' accounting month.
            f = next_business_day(dd, cal)
            if f.month != dd.month then
                return previous_business_day(dd, cal)
            end if
            return f
        end if
        error "dates: roll must be forward, backward, or modified"
    end function

    function _finish(dd, spec, cal)
        rolled = _apply_roll(dd, spec, cal)
        if has(spec, "at") then
            return at(rolled, spec.at)
        end if
        return rolled
    end function

    function matches(d, spec, cal)
        dd (day)= d
        if not _candidate(dd, spec, cal) then
            return false
        end if
        if has(spec, "after") then
            if not (dd > _bound(spec.after, dd)) then
                return false
            end if
        end if
        if has(spec, "on_or_after") then
            if not (dd >= _bound(spec.on_or_after, dd)) then
                return false
            end if
        end if
        if has(spec, "before") then
            if not (dd < _bound(spec.before, dd)) then
                return false
            end if
        end if
        if has(spec, "on_or_before") then
            if not (dd <= _bound(spec.on_or_before, dd)) then
                return false
            end if
        end if
        if has(spec, "nth") then
            within = "month"
            if has(spec, "within") then
                within = spec.within
            end if
            sc = _scope(dd, within)
            total = 0
            mypos = 0
            cur = sc.first
            while cur <= sc.last
                if _candidate(cur, spec, cal) then
                    total = total + 1
                    if cur = dd then
                        mypos = total
                    end if
                end if
                cur = cur + 1 day
            end while
            n = _nth_num(spec.nth)
            if n > 0 then
                if mypos != n then
                    return false
                end if
            else
                if mypos != total + 1 + n then
                    return false
                end if
            end if
        end if
        return true
    end function

    function select(spec, anchor, cal)
        aa (day)= anchor
        n = 1
        if has(spec, "nth") then
            n = _nth_num(spec.nth)
        end if
        dir = 0
        bound = aa
        inclusive = false
        if has(spec, "after") then
            dir = 1
            bound = _bound(spec.after, aa)
        end if
        if has(spec, "on_or_after") then
            dir = 1
            inclusive = true
            bound = _bound(spec.on_or_after, aa)
        end if
        if has(spec, "before") then
            dir = 0 - 1
            bound = _bound(spec.before, aa)
        end if
        if has(spec, "on_or_before") then
            dir = 0 - 1
            inclusive = true
            bound = _bound(spec.on_or_before, aa)
        end if
        if dir = 0 and not has(spec, "nth") and not has(spec, "within") then
            ' Bare spec: the (next X) reading -- first candidate STRICTLY
            ' after the anchor, matching the lenses' exclusive convention.
            dir = 1
        end if
        if dir != 0 then
            if n < 1 then
                error "dates.select: nth must be positive when searching from an anchor"
            end if
            dd = bound
            if not inclusive then
                dd = dd + (1 day) * dir
            end if
            seen = 0
            guard = 0
            while guard <= 3700
                if _candidate(dd, spec, cal) then
                    seen = seen + 1
                    if seen = n then
                        return _finish(dd, spec, cal)
                    end if
                end if
                dd = dd + (1 day) * dir
                guard = guard + 1
            end while
            return unknown                            ' a miss, not an error
        end if
        if not has(spec, "nth") then
            error "dates.select: nth is required with within (or use after/before)"
        end if
        within = "month"
        if has(spec, "within") then
            within = spec.within
        end if
        sc = _scope(aa, within)
        found = []
        cur = sc.first
        while cur <= sc.last
            if _candidate(cur, spec, cal) then
                append(found, cur)
            end if
            cur = cur + 1 day
        end while
        idx = n
        if n < 0 then
            idx = count(found) + 1 + n
        end if
        if idx < 1 or idx > count(found) then
            return unknown                            ' e.g. the fifth Tuesday
        end if
        return _finish(found[idx - 1], spec, cal)
    end function

    function _step_to(start, every, k, prev, cal)
        if type(every) = "duration" then
            return start + every * k
        end if
        if every = "day" then
            return start + (1 day) * k
        end if
        if every = "week" then
            return start + (7 days) * k
        end if
        if every = "month" then
            return start + (1 month) * k
        end if
        if every = "quarter" then
            return start + (3 months) * k
        end if
        if every = "year" then
            return start + (1 year) * k
        end if
        if every = "business day" then
            return next_business_day(prev, cal)
        end if
        error "dates.series: every must be a duration or one of day, week, month, quarter, year, business day"
    end function

    ' Every candidate day inside sub's scope around the anchor, in order.
    ' This is what `when:` WITHOUT nth: means in a series: not "the nth such
    ' day each period" but "EVERY such day" -- Mon/Wed/Fri standups, all
    ' business days of the month, the 1st and 15th. (RRULE calls this BYDAY
    ' with no BYSETPOS.)
    function _candidates_in(sub, anchor, cal)
        sc = _scope(anchor, sub.within)
        found = []
        cur = sc.first
        while cur <= sc.last
            if _candidate(cur, sub, cal) then
                append(found, cur)
            end if
            cur = cur + 1 day
        end while
        return found
    end function

    function _period(unit, k)
        if unit = "week" then
            return (7 days) * k
        end if
        if unit = "month" then
            return (1 month) * k
        end if
        if unit = "quarter" then
            return (3 months) * k
        end if
        return (1 year) * k
    end function

    function series(spec, bounds, cal)
        start (day)= bounds.from
        want = 0 - 1
        if has(bounds, "count") then
            want = bounds.count
        end if
        has_through = has(bounds, "through")
        stop_at = start
        if has_through then
            stop_at (day)= bounds.through
        end if
        if not has_through and want < 1 then
            error "dates.series: bounds need through: or count:"
        end if
        out = []
        if has(spec, "when") then
            ' Period mode: every period, the day the sub-rule picks.
            unit = ""
            if has(spec, "every") then
                unit = spec.every
            end if
            if unit != "week" and unit != "month" and unit != "quarter" and unit != "year" then
                error "dates.series: when: needs every: week, month, quarter, or year"
            end if
            sub = spec.when
            sub.within = unit
            sc = _scope(start, unit)
            k = 0
            while k < 10000
                anchor = sc.first + _period(unit, k)
                if has_through and anchor > stop_at then
                    return out
                end if
                if has(sub, "nth") then
                    picked = [select(sub, anchor, cal)]
                else
                    ' No nth: EVERY candidate in the period, in order.
                    picked = _candidates_in(sub, anchor, cal)
                end if
                for each d in picked
                    if not is_unknown(d) then
                        dd (day)= d
                        if dd >= start then
                            emit = true
                            if has_through and dd > stop_at then
                                return out
                            end if
                            if _excepted(dd, spec) then
                                emit = false           ' a gap, not a reschedule
                            end if
                            if emit then
                                append(out, _finish(dd, spec, cal))
                                if want > 0 and count(out) >= want then
                                    return out
                                end if
                            end if
                        end if
                    end if
                end for
                k = k + 1
            end while
            return out
        end if
        ' Stepping mode. Steps are MULTIPLICATIVE from the start -- start +
        ' step*k, never cumulative -- so monthly from Jan 31 gives Feb 28 then
        ' MAR 31, not the Feb-28-forever drift that cumulative clamping causes.
        if not has(spec, "every") then
            error "dates.series: spec needs every: (or when: with every:)"
        end if
        if type(spec.every) = "duration" then
            if spec.every = (0 seconds) then
                error "dates.series: every cannot be zero"
            end if
        end if
        dd = start
        if type(spec.every) = "string" then
            if spec.every = "business day" then
                if not is_business_day(dd, cal) then
                    dd = next_business_day(dd, cal)
                end if
            end if
        end if
        k = 0
        while k < 10000
            if has_through and dd > stop_at then
                return out
            end if
            if not _excepted(dd, spec) then
                append(out, _finish(dd, spec, cal))
                if want > 0 and count(out) >= want then
                    return out
                end if
            end if
            k = k + 1
            dd = _step_to(start, spec.every, k, dd, cal)
        end while
        return out
    end function

    ' ------------------------------------------------------------------
    ' Business-hours arithmetic (docs/datetime_design.md §9): working time
    ' that pauses overnight, across weekends and holidays. "Respond within
    ' 4 business hours" is add_business_hours; "how much working time
    ' elapsed" is business_hours_between. Both need cal.hours.
    '
    ' The rules, decided and stated:
    '  * a clock STARTING outside working hours starts at the next open
    '    (and, going backward, at the previous close);
    '  * a deadline that exhausts its time EXACTLY at close lands AT close --
    '    rolling it to the next morning would silently extend an SLA, and
    '    landing at close is what makes the round-trip law hold:
    '    business_hours_between(a, add_business_hours(a, n, cal), cal) = n;
    '  * durations must be exact -- a month of business hours has no meaning;
    '  * negative durations walk backward (the honest algebra, as everywhere).

    function _need_hours(cal)
        if not has(cal, "hours") then
            error "dates: business-hours arithmetic needs a calendar with hours: { open:, close: }"
        end if
        return cal.hours
    end function

    function _day_open(d, cal)
        return at(d, cal.hours.open)
    end function

    function _day_close(d, cal)
        return at(d, cal.hours.close)
    end function

    ' Is this instant inside working hours on a business day? The window is
    ' half-open: the open instant is working time, the close instant is not.
    function is_business_time(d, cal)
        h = _need_hours(cal)
        if not is_business_day(d, cal) then
            return false
        end if
        return d >= _day_open(d, cal) and d < _day_close(d, cal)
    end function

    ' The nearest working instant at or after d.
    function _clock_forward(d, cal)
        if is_business_day(d, cal) then
            if d < _day_open(d, cal) then
                return _day_open(d, cal)
            end if
            if d < _day_close(d, cal) then
                return d
            end if
        end if
        return _day_open(next_business_day(d, cal), cal)
    end function

    ' The nearest working instant at or before d (for negative durations).
    function _clock_backward(d, cal)
        if is_business_day(d, cal) then
            if d > _day_close(d, cal) then
                return _day_close(d, cal)
            end if
            if d > _day_open(d, cal) then
                return d
            end if
        end if
        return _day_close(previous_business_day(d, cal), cal)
    end function

    function add_business_hours(d, dur, cal)
        h = _need_hours(cal)
        if dur.years != 0 or dur.months != 0 then
            error "dates: business-hours arithmetic needs an exact duration; a month has no fixed length"
        end if
        remaining = dur.total_seconds
        if remaining >= 0 then
            cur = _clock_forward(d, cal)
            guard = 0
            while guard < 10000
                guard = guard + 1
                room = _day_close(cur, cal) - cur
                if remaining <= room.total_seconds then
                    return cur + (1 second) * remaining
                end if
                remaining = remaining - room.total_seconds
                cur = _day_open(next_business_day(cur, cal), cal)
            end while
            error "dates: business-hours arithmetic exceeded 10000 working days"
        end if
        remaining = 0 - remaining
        cur = _clock_backward(d, cal)
        guard = 0
        while guard < 10000
            guard = guard + 1
            room = cur - _day_open(cur, cal)
            if remaining <= room.total_seconds then
                return cur - (1 second) * remaining
            end if
            remaining = remaining - room.total_seconds
            cur = _day_close(previous_business_day(cur, cal), cal)
        end while
        error "dates: business-hours arithmetic exceeded 10000 working days"
    end function

    ' Working time elapsed over (a, b), as an exact duration. Signed. Each
    ' business day contributes the overlap of [a, b] with its working window.
    function business_hours_between(a, b, cal)
        h = _need_hours(cal)
        if b < a then
            return (0 seconds) - business_hours_between(b, a, cal)
        end if
        total = 0
        dd (day)= a
        last (day)= b
        guard = 0
        while dd <= last and guard < 10000
            guard = guard + 1
            if is_business_day(dd, cal) then
                seg_start = _day_open(dd, cal)
                if a > seg_start then
                    seg_start = a
                end if
                seg_end = _day_close(dd, cal)
                if b < seg_end then
                    seg_end = b
                end if
                if seg_end > seg_start then
                    piece = seg_end - seg_start
                    total = total + piece.total_seconds
                end if
            end if
            dd = dd + 1 day
        end while
        return (1 second) * total
    end function
end library
