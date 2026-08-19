# The datetime cookbook — dates, durations, calendars and schedules in gBASIC

Status: **every example on this page is a file in `examples/datetime_cookbook/`,
run by `tests/run_datetime_cookbook.sh` on every pass.** The code and output
blocks below are checked byte-for-byte against those files and their recorded
output, so this page cannot drift from what the language actually does. If you
change a recipe, run `tools/sync_datetime_cookbook.sh`; the suite fails until
the page agrees again.

Everything here is core gBASIC plus two Apache-licensed standard libraries,
`dates` and `schedule`. No optional native dependency is involved. The design
behind it all is `docs/datetime_design.md`.

```sh
GBASIC_PATH=stdlib ./gbasic examples/datetime_cookbook/06_date_expressions.bas
```

---

## What makes gBASIC's model different

Three ideas carry everything on this page:

1. **One datetime kind, carrying its precision.** `2026`, `2026-03` and
   `2026-03-15` are the same kind at different precisions — what Java needs
   four types for. Lenses (`(day)=`, `(month)=`) truncate; dot fields
   (`d.year`, `d.weekday`) extract numbers.
2. **Exact and calendar time are never blurred.** Hours and days have fixed
   lengths; months do not. Month arithmetic clamps at month-end (the
   accountant's rule), and asking for an order between `1 month` and `30 days`
   is refused rather than answered plausibly.
3. **Rules are data.** A recurrence rule, a business calendar, a session plan —
   each is an ordinary record you can store in a file, build in code, send to
   an actor, and print when something looks wrong.

## 1. Datetimes carry their precision; durations are real values

<!--CODE:01_dates_and_durations-->

```basic
' Recipe 1 — Datetimes carry their precision; durations are real values.
'
' One datetime kind covers what other languages need four types for: a year,
' a month, a date and a timestamp are the SAME kind at different precisions,
' and the precision decides how the value renders. There are no date literals
' (2026-12-25 would parse as subtraction!) -- the (date)= modifier takes an
' ISO string at any precision.

program main(args)
    y (date)= "2026"
    m (date)= "2026-03"
    d (date)= "2026-03-15"
    t (date)= "2026-03-15 09:30"

    print "year value : " + y
    print "month value: " + m
    print "date value : " + d
    print "minute prec: " + t

    ' Durations are values too, written the way you would say them.
    print ""
    print "durations  : " + (90 minutes) + " / " + (2 days) + " / " + (1 hour 20 minutes)

    ' Adding a duration respects the calendar -- THE ACCOUNTANT'S RULE:
    ' years and months first, then the day is CLAMPED into the resulting
    ' month, then exact parts. Month-end behaves the way an invoice expects.
    jan31 (date)= "2026-01-31"
    print ""
    print "Jan 31 + 1 month  = " + (jan31 + 1 month)
    print "Jan 31 + 3 months = " + (jan31 + (1 month) * 3)
    feb29 (date)= "2024-02-29"
    print "Feb 29 + 1 year   = " + (feb29 + 1 year)
    print "back off month-end: " + (jan31 + 1 month - 1 day)
end program
```

<!--OUT:01_dates_and_durations-->

```
year value : 2026
month value: 2026-03
date value : 2026-03-15
minute prec: 2026-03-15 09:30

durations  : 90 minutes / 2 days / 1 hour 20 minutes

Jan 31 + 1 month  = 2026-02-28
Jan 31 + 3 months = 2026-04-30
Feb 29 + 1 year   = 2025-02-28
back off month-end: 2026-02-27
```

There is deliberately **no date literal**: `d = 2026-12-25` already means
`2026 − 12 − 25` (which is `1989`), so the modifier form takes an ISO string —
at *any* precision, `"2026"` through `"2026-03-15 09:30:45"`.

The clamping rule is normative: years and months are added first, the day is
clamped into the resulting month, and only then are exact parts applied. So
`Jan 31 + 1 month + 1 day` is `Mar 1` — clamp first, then the day. The
round-trip `(d + 1 month) - 1 month` deliberately does **not** hold at
month-end; clamping is lossy, and pretending otherwise would be worse.

## 2. Getting numbers out

<!--CODE:02_components-->

```basic
' Recipe 2 — Getting numbers OUT: dot fields extract, lenses truncate.
'
' d.year is a NUMBER you can compute with; (year)= gives a coarser DATETIME
' for grouping. Two mechanisms, two jobs, zero global names spent.

program main(args)
    d (date)= "2026-03-15 09:30:45"

    print "year " + d.year + ", month " + d.month + ", day " + d.day
    print "clock " + d.hour + ":" + d.minute + ":" + d.second

    ' Weekday is ISO 8601: Monday=1 .. Sunday=7 (no zero -- Sunday=0 is the
    ' C/JavaScript convention). That makes the workday test one comparison.
    print "weekday " + d.weekday + " (" + d.dayname + ")"
    print "is a workday: " + (d.weekday <= 5)
    print "day of year : " + d.day_of_year

    ' The time of day is an exact DURATION since midnight -- no separate kind.
    print "time of day : " + d.time

    ' THE PRECISION RULE: a field finer than the value's precision is absent
    ' information and reads as unknown -- never a plausible zero.
    m (month)= d
    print ""
    print "month value      : " + m + "  (precision " + m.precision + ")"
    print "its .day is known: " + (not is_unknown(m.day))
end program
```

<!--OUT:02_components-->

```
year 2026, month 3, day 15
clock 9:30:45
weekday 7 (Sunday)
is a workday: false
day of year : 74
time of day : 9 hours 30 minutes 45 seconds

month value      : 2026-03  (precision month)
its .day is known: false
```

**The precision rule** is the part to internalise: a field finer than the
value's precision reads as `unknown` — a month value has no meaningful `.day`,
and `unknown` is how gBASIC says *absent*, never a plausible zero. A
misspelled field *name* raises instead, so a typo cannot masquerade as absence.

`d.weekday` follows ISO 8601 — Monday=1 through Sunday=7, no zero. (Sunday=0
is the C/JavaScript convention, Sunday=1 is Excel's.) The payoff is that
`d.weekday <= 5` *is* the workday test.

## 3. Duration algebra — exact vs calendar, kept honest

<!--CODE:03_duration_algebra-->

```basic
' Recipe 3 — Duration algebra: exact and calendar parts, never blurred.
'
' A duration has an exact part (weeks/days/hours/minutes/seconds) and a
' calendar part (years, months). Exact parts have a fixed length; a month
' does NOT. gBASIC keeps the two honest: exact durations order and total,
' month-bearing ones refuse to pretend.

program main(args)
    print "1h + 30m        = " + ((1 hour) + (30 minutes))
    print "2h - 30m        = " + ((2 hours) - (30 minutes))
    print "45m * 4         = " + ((45 minutes) * 4)
    print "1 day / 2       = " + ((1 day) / 2)
    print "signed          = " + ((30 minutes) - (2 hours))

    print ""
    print "90m > 1h        : " + ((90 minutes) > (1 hour))
    print "25h > 1 day     : " + ((25 hours) > (1 day))
    print "90m = 1h30m     : " + ((90 minutes) = (1 hour 30 minutes))
    print "1 week = 7 days : " + ((1 week) = (7 days))
    print "1 month = 30d   : " + ((1 month) = (30 days))

    ' Ordering a month-bearing duration is REFUSED, not guessed -- catch the
    ' refusal to see its message.
    on error resume next
    x = (1 month) > (30 days)
    print ""
    print "refused: " + error.message
    error.clear()

    ' total_seconds works only where a total exists.
    t = 1 hour 30 minutes
    print "total_seconds   = " + t.total_seconds
    mth = 1 month
    on error resume next
    x = mth.total_seconds
    print "refused: " + error.message
    error.clear()
end program
```

<!--OUT:03_duration_algebra-->

```
1h + 30m        = 1 hour 30 minutes
2h - 30m        = 1 hour 30 minutes
45m * 4         = 3 hours
1 day / 2       = 12 hours
signed          = -1 hour -30 minutes

90m > 1h        : true
25h > 1 day     : true
90m = 1h30m     : true
1 week = 7 days : true
1 month = 30d   : false

refused: a month has no fixed length; compare exact durations or use dates.between
total_seconds   = 5400
refused: a month has no fixed length; total_seconds requires an exact duration
```

Results are canonical (`45m × 4` renders as `3 hours`), durations are signed
(`30m − 2h` is honestly negative), and equality canonicalises what genuinely
has one length: `1 week = 7 days`, `1 year = 12 months`, `90 minutes = 1 hour
30 minutes` — but **`1 month = 30 days` is false**, because it isn't true.

The refusals are the feature. Before this design, *every* duration equality
returned true and *every* ordering returned false — both sides silently became
0. Now the impossible question gets an error that says where to go instead.

## 4. How far apart? Two questions, two answers

<!--CODE:04_deadlines_and_ages-->

```basic
' Recipe 4 — How far apart? Two different questions, two different answers.
'
' datetime - datetime gives the EXACT elapsed time, in days and smaller --
' never months, because a month has no fixed length. "How many months apart"
' is a CALENDAR question, answered by dates.between, which is consistent with
' the clamping rule by construction.

program main(args)
    load dates from "../../stdlib/dates.bas"

    a (date)= "2026-03-14 08:30:00"
    b (date)= "2026-03-15 10:00:00"
    print "exact gap    : " + (b - a)
    print "backwards    : " + (a - b)

    ' Days until a deadline -- exact division of an exact duration.
    today (date)= "2026-08-17"
    deadline (date)= "2026-09-30"
    gap = deadline - today
    print "days left    : " + (gap.total_seconds / 86400)
    print "same, direct : " + dates.between(today, deadline, "days")

    ' Ages and anniversaries are calendar arithmetic.
    born (date)= "1990-06-15"
    print "age in years : " + dates.between(born, today, "years")
    print "age in months: " + dates.between(born, today, "months")

    ' The month count agrees with the operator, clamping included: Jan 31 ->
    ' Feb 28 counts as one month exactly BECAUSE Jan 31 + 1 month is Feb 28.
    jan31 (date)= "2026-01-31"
    feb28 (date)= "2026-02-28"
    print "Jan31->Feb28 : " + dates.between(jan31, feb28, "months") + " month(s)"
end program
```

<!--OUT:04_deadlines_and_ages-->

```
exact gap    : 1 day 1 hour 30 minutes
backwards    : -1 day -1 hour -30 minutes
days left    : 44
same, direct : 44
age in years : 36
age in months: 434
Jan31->Feb28 : 1 month(s)
```

`b - a` answers in **exact** time — days and smaller, never months.
`dates.between(a, b, "months" | "years" | "days")` answers the **calendar**
question, and its month count is defined as the largest `k` with
`a + (1 month)*k` on or before `b` — the core clamped operator itself — so the
two ways of talking about months can never disagree.

## 5. Business calendars are data

<!--CODE:05_business_calendar-->

```basic
' Recipe 5 — A business calendar is data you build and pass around.
'
' Not global configuration: two teams can hold two calendars in one program,
' and the holiday list can come from a file or a database. The constructor
' supplies the defaults (Sat/Sun weekend, no holidays) and normalises
' holidays to day precision -- a holiday supplied as a full timestamp still
' blocks the whole day.

program main(args)
    load dates from "../../stdlib/dates.bas"

    cal = dates.calendar({})
    mon (date)= "2026-08-17"
    sat (date)= "2026-08-15"
    print "Monday works : " + dates.is_business_day(mon, cal)
    print "Saturday not : " + dates.is_business_day(sat, cal)

    ' Christmas 2026 is a Friday. From Christmas Eve, the next business day
    ' steps over the holiday AND the weekend.
    xmas_stamp (date)= "2026-12-25 09:30:00"
    hcal = dates.calendar({ holidays: [xmas_stamp] })
    eve (date)= "2026-12-24"
    print "after Dec 24 : " + dates.next_business_day(eve, hcal)

    ' Deadlines in working days, both directions.
    print "5 bdays on   : " + dates.add_business_days(mon, 5, hcal)
    print "1 bday back  : " + dates.add_business_days(mon, 0 - 1, hcal)

    ' Working days until a date: counted over (a, b], signed. The convention
    ' is worth knowing -- half-open intervals are where calendar bugs live.
    fri (date)= "2026-08-21"
    print "Mon..Fri     : " + dates.business_days_between(mon, fri, cal) + " working days"
end program
```

<!--OUT:05_business_calendar-->

```
Monday works : true
Saturday not : false
after Dec 24 : 2026-12-28
5 bdays on   : 2026-08-24
1 bday back  : 2026-08-14
Mon..Fri     : 4 working days
```

`dates.calendar(spec)` supplies the defaults (Sat/Sun weekend, no holidays)
and normalises holidays to day precision once, at construction. Note the
convention on `business_days_between`: it counts over `(a, b]` — half-open
intervals are where calendar bugs live, so the choice is stated rather than
discovered.

Working hours are `open`/`close`, not `start`/`end` — `end` is a keyword that
can be a record key but cannot follow a dot, so `cal.hours.end` would not
parse. The same rule renames two spec fields below.

## 6. Date expressions: one spec, three verbs

<!--CODE:06_date_expressions-->

```basic
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
```

<!--OUT:06_date_expressions-->

```
3rd Thursday      : 2026-08-20
last Wednesday    : 2026-08-26
1st Tue after 15th: 2026-08-18
1st Mon of quarter: 2026-07-06
on_or_after Mon   : 2026-08-17
after Mon         : 2026-08-24
last bday before  : 2026-12-24
5th Tuesday       : missing = true
is 3rd Thursday?  : true
```

The vocabulary, in one place:

| Field | Meaning |
|---|---|
| `weekday:` | `"thursday"`, or a list |
| `nth:` | 1, 2, 3… from the start; −1 (or `"last"`) from the end |
| `day:` | day of month |
| `kind:` | `"business"` — business days under the calendar |
| `within:` | `"week"` \| `"month"` \| `"quarter"` \| `"year"` — the scope `nth` counts in |
| `after:` / `on_or_after:` / `before:` / `on_or_before:` | anchors — **strictness is in the name**; a bound may be a datetime or `{ day: 15 }` |
| `at:` | a time of day to stamp — `"14:00"` or an exact duration |
| `roll:` | `"forward"` \| `"backward"` \| `"modified"` when the day isn't a business day |
| `except:` | days to skip |

Two failure modes, deliberately different: a spec no day satisfies yields
**`unknown`** (the fifth Tuesday); a *malformed* spec **raises**. Probing and
misconfiguration are not the same thing.

## 7. Recurring schedules

<!--CODE:07_recurring_schedules-->

```basic
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
```

<!--OUT:07_recurring_schedules-->

```
board: 2026-01-15 14:00:00
board: 2026-02-19 14:00:00
board: 2026-03-19 14:00:00
board: 2026-04-16 14:00:00
board: 2026-05-21 14:00:00
board: 2026-06-18 14:00:00

pay:   2026-01-02
pay:   2026-01-16
pay:   2026-01-30
pay:   2026-02-12
pay:   2026-02-27
pay:   2026-03-13

standup: 2026-08-17 09:15:00
standup: 2026-08-19 09:15:00
standup: 2026-08-21 09:15:00
standup: 2026-08-24 09:15:00
standup: 2026-08-26 09:15:00

bill:  2026-01-31
bill:  2026-02-28
bill:  2026-03-31
bill:  2026-04-30
```

`series` adds two fields: `every:` (the rhythm — a unit word, a duration like
`2 weeks`, or `"business day"`) and `when:` (which day inside each period).
Bounds are `{ from:, through: }` — *inclusive*, as the name says — or
`{ from:, count: }`.

`when:` with `nth:` picks one day per period; **without `nth:` it emits every
matching day** — that is how the Mon/Wed/Fri standup reads as one rule instead
of three merged series.

Steps are **multiplicative from the start** (`start + step×k`), never
cumulative — that is why the month-end billing run goes Jan 31 → Feb 28 →
**Mar 31**, instead of drifting to the 28th forever after one February.
`roll: "backward"` on the payroll run means pay is never late; `except:`
removes a date and leaves a **gap**, not a reschedule.

(Why `when:` and `through:` rather than `on:` and `to:`: `on` and `to` are
keywords, and a keyword can be a record key but cannot follow a dot — the
library could never read `spec.on` back.)

## 8. A day that works for everyone

<!--CODE:08_mutual_calendars-->

```basic
' Recipe 8 — Finding a day that works for everyone.
'
' Merging calendars is a UNION OF CONSTRAINTS -- more weekend, more holidays,
' a narrower hours window -- so a merged calendar plugs into every verb that
' takes one. Finding a mutual meeting day needs no new machinery, and the
' guarantee is a law: a day is a business day in the merge exactly when it is
' one in EVERY constituent.

program main(args)
    load dates from "../../stdlib/dates.bas"

    xmas (date)= "2026-12-25"
    alice = dates.calendar({ holidays: [xmas], hours: { open: "9:00", close: "17:00" } })
    bob = dates.calendar({ weekend: ["friday", "saturday", "sunday"], hours: { open: "10:00", close: "16:30" } })

    both = dates.merge([alice, bob])
    print "merged weekend : " + both.weekend
    print "merged window  : " + both.hours.open + " to " + both.hours.close

    ' First day they can both meet, starting from Wed Dec 23.
    wed (date)= "2026-12-23"
    day = dates.next_business_day(wed, both)
    print "mutual day     : " + day
    print "works for Alice: " + dates.is_business_day(day, alice)
    print "works for Bob  : " + dates.is_business_day(day, bob)

    ' Their whole mutual week, as a series over the merged calendar.
    print ""
    for each d in dates.series({ every: "business day" }, { from: wed, count: 4 }, both)
        print "slot day: " + d + " (" + d.dayname + ")"
    end for
end program
```

<!--OUT:08_mutual_calendars-->

```
merged weekend : ["saturday","sunday","friday"]
merged window  : 10:00 to 16:30
mutual day     : 2026-12-24
works for Alice: true
works for Bob  : true

slot day: 2026-12-23 (Wednesday)
slot day: 2026-12-24 (Thursday)
slot day: 2026-12-28 (Monday)
slot day: 2026-12-29 (Tuesday)
```

`dates.merge` unions the *constraints* — more weekend, more holidays, the
narrower hours window — so the merged calendar plugs into every verb that
takes one. The guarantee is a law the test suite checks by arithmetic:
`is_business_day(d, merge([a,b]))` equals
`is_business_day(d,a) and is_business_day(d,b)`, for every `d`.

## 9. Laying out a convention

<!--CODE:09_convention_layout-->

```basic
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
```

<!--OUT:09_convention_layout-->

```
day 1  2026-08-17 09:00:00 .. 2026-08-17 10:30:00  Opening keynote
day 1  2026-08-17 13:00:00 .. 2026-08-17 15:00:00  Workshop A
day 1  2026-08-17 15:15:00 .. 2026-08-17 16:00:00  Panel
day 2  2026-08-18 09:00:00 .. 2026-08-18 10:30:00  Deep dive
day 2  2026-08-18 10:45:00 .. 2026-08-18 11:45:00  Closing
UNPLACED: All-day marathon (fits in no working day)
```

The layout rules are decided, not discovered: sessions keep their order; a
session that misses the day end moves **whole** to the next day; breaks are
immovable, and a bumped session resumes exactly at break end — the gap belongs
*between sessions*, not around breaks; and a session that fits in no working
day is reported in `unplaced:` **by name**, without sinking the sessions
behind it.

## 10. An appointment book

<!--CODE:10_appointment_slots-->

```basic
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
    mon (date)= "2026-08-17"

    grid = schedule.slots(mon, { length: 20 minutes, gap: 10 minutes, breaks: [ { at: "12:00", length: 1 hour } ] }, cal)

    print count(grid) + " bookable slots on " + mon + ":"
    for each sl in grid
        print "  " + sl.starts + " .. " + sl.ends
    end for
end program
```

<!--OUT:10_appointment_slots-->

```
14 bookable slots on 2026-08-17:
  2026-08-17 09:00:00 .. 2026-08-17 09:20:00
  2026-08-17 09:30:00 .. 2026-08-17 09:50:00
  2026-08-17 10:00:00 .. 2026-08-17 10:20:00
  2026-08-17 10:30:00 .. 2026-08-17 10:50:00
  2026-08-17 11:00:00 .. 2026-08-17 11:20:00
  2026-08-17 11:30:00 .. 2026-08-17 11:50:00
  2026-08-17 13:00:00 .. 2026-08-17 13:20:00
  2026-08-17 13:30:00 .. 2026-08-17 13:50:00
  2026-08-17 14:00:00 .. 2026-08-17 14:20:00
  2026-08-17 14:30:00 .. 2026-08-17 14:50:00
  2026-08-17 15:00:00 .. 2026-08-17 15:20:00
  2026-08-17 15:30:00 .. 2026-08-17 15:50:00
  2026-08-17 16:00:00 .. 2026-08-17 16:20:00
  2026-08-17 16:30:00 .. 2026-08-17 16:50:00
```

Fourteen slots: the 30-minute cycle tiles the morning six times, lunch is
excluded entirely, and the afternoon holds eight — the last ending 16:50,
inside the window. *Which* slots are booked is application state; a booking
system stores slot starts against patients and filters this grid.

## 11. Timezones: UTC for the timeline, civil for the calendar

<!--CODE:11_timezones_at_the_edges-->

```basic
' Recipe 11 — Timezones: UTC for the timeline, civil for the calendar, zone
' names at the edges.
'
' The rule below is stored as CIVIL time plus a zone NAME -- never as UTC
' instants. Watch why: the Chicago board meeting is 14:00 local all year, but
' its UTC image SHIFTS an hour when DST ends in November. Stored as 19:00Z,
' the November meetings would silently move. Each occurrence converts with
' its own offset, which is what makes DST arithmetic automatic.

program main(args)
    load dates

    cal = dates.calendar({})
    rule = { every: "month", when: { nth: 3, weekday: "thursday" }, at: "14:00" }
    sep1 (date)= "2026-09-01"
    dec31 (date)= "2026-12-31"

    print "Board meets every 3rd Thursday, 14:00 America/Chicago:"
    for each m in dates.series(rule, { from: sep1, through: dec31 }, cal)
        utc = from_zone(m, "America/Chicago")
        print "  " + m + " local = " + utc + "Z = " + to_zone(utc, "Europe/Berlin") + " Berlin"
    end for

    ' Note the UTC column shifts in November (Chicago leaves DST) while the
    ' Berlin column holds steady -- Europe shifted a week earlier, so the two
    ' zones moved almost together. Only the UTC image jumped. That is the §9
    ' argument in four rows.

    print ""
    off_oct (date)= "2026-10-15 12:00:00"
    off_nov (date)= "2026-11-15 12:00:00"
    print "Chicago offset in Oct: " + zone_offset(off_oct, "America/Chicago")
    print "Chicago offset in Nov: " + zone_offset(off_nov, "America/Chicago")

    ' DST edge cases are NAMED, never guessed. 02:30 on the spring-forward
    ' night does not exist; the fall-back 01:30 happens twice.
    print ""
    gap (date)= "2026-03-08 02:30:00"
    r = zone_resolve(gap, "America/New_York")
    print "2026-03-08 02:30 New York is " + r.kind
    twice (date)= "2026-11-01 01:30:00"
    r2 = zone_resolve(twice, "America/New_York")
    print "2026-11-01 01:30 New York is " + r2.kind + " (earlier " + r2.earlier + "Z, later " + r2.later + "Z)"
end program
```

<!--OUT:11_timezones_at_the_edges-->

```
Board meets every 3rd Thursday, 14:00 America/Chicago:
  2026-09-17 14:00:00 local = 2026-09-17 19:00:00Z = 2026-09-17 21:00:00 Berlin
  2026-10-15 14:00:00 local = 2026-10-15 19:00:00Z = 2026-10-15 21:00:00 Berlin
  2026-11-19 14:00:00 local = 2026-11-19 20:00:00Z = 2026-11-19 21:00:00 Berlin
  2026-12-17 14:00:00 local = 2026-12-17 20:00:00Z = 2026-12-17 21:00:00 Berlin

Chicago offset in Oct: -5 hours
Chicago offset in Nov: -6 hours

2026-03-08 02:30 New York is nonexistent
2026-11-01 01:30 New York is ambiguous (earlier 2026-11-01 05:30:00Z, later 2026-11-01 06:30:00Z)
```

The doctrine, in four rows: the meeting is **14:00 Chicago, always** — civil
time plus a zone name. Its UTC image jumps an hour in November; the Berlin
rendering holds at 21:00 because Europe shifted a week earlier. Store the rule
and the zone, compute occurrences civil, convert each with **its own** offset
(`from_zone` per occurrence), keep UTC on the wire, and render per user with
`to_zone`. Storing future meetings as UTC instants would have silently moved
the November ones.

Three refusals guard the edges: an **unknown zone** raises (glibc would
otherwise fall back to UTC silently — a typo must not become quietly-UTC
arithmetic); an **all-day value** raises (a due date has no instant, and
midnight-in-a-zone is the classic off-by-one-day bug); and the DST edge cases
are **named** — `zone_resolve` reports `unique`/`ambiguous`/`nonexistent` with
both candidate instants, while `from_zone`'s default takes the earlier instant
for the repeated hour and shifts forward through the gap (Temporal's
"compatible" policy).

## 12. SLA clocks: arithmetic in working time

<!--CODE:12_sla_clocks-->

```basic
' Recipe 12 — SLA clocks: working time that pauses nights, weekends, holidays.
'
' "Respond within 4 business hours" is a deadline computed in WORKING time.
' The clock starts at the next open if the ticket arrives after hours; a
' deadline that exhausts its time exactly at close is due AT close (rolling
' it to next morning would silently extend the SLA); and the whole thing
' round-trips: business_hours_between(a, add_business_hours(a, n, cal)) = n.

program main(args)
    load dates

    cal = dates.calendar({ hours: { open: "9:00", close: "17:00" } })

    ' Four tickets, one promise: respond within 4 business hours.
    t1 (date)= "2026-08-17 09:30:00"
    t2 (date)= "2026-08-17 15:00:00"
    t3 (date)= "2026-08-14 16:00:00"
    t4 (date)= "2026-08-15 11:00:00"
    for each t in [t1, t2, t3, t4]
        due = dates.add_business_hours(t, 4 hours, cal)
        print "in " + t + " (" + t.dayname + ")  ->  due " + due + " (" + due.dayname + ")"
    end for

    ' How much working time did a resolution actually take?
    opened (date)= "2026-08-14 15:00:00"
    closed (date)= "2026-08-17 11:00:00"
    print ""
    print "opened Friday 15:00, closed Monday 11:00 = " + dates.business_hours_between(opened, closed, cal) + " of work time"

    ' The clock respects holidays like every other calendar verb.
    xmas (date)= "2026-12-25"
    hcal = dates.calendar({ holidays: [xmas], hours: { open: "9:00", close: "17:00" } })
    eve (date)= "2026-12-24 15:00:00"
    print "Christmas Eve 15:00 + 4 business hours = " + dates.add_business_hours(eve, 4 hours, hcal)
end program
```

<!--OUT:12_sla_clocks-->

```
in 2026-08-17 09:30:00 (Monday)  ->  due 2026-08-17 13:30:00 (Monday)
in 2026-08-17 15:00:00 (Monday)  ->  due 2026-08-18 11:00:00 (Tuesday)
in 2026-08-14 16:00:00 (Friday)  ->  due 2026-08-17 12:00:00 (Monday)
in 2026-08-15 11:00:00 (Saturday)  ->  due 2026-08-17 13:00:00 (Monday)

opened Friday 15:00, closed Monday 11:00 = 4 hours of work time
Christmas Eve 15:00 + 4 business hours = 2026-12-28 11:00:00
```

The decided rules: the clock **starts** at the next open when a ticket arrives
after hours (and, walking backward with a negative duration, at the previous
close); a deadline exhausting its time exactly at close is due **at close** —
rolling it to next morning would silently extend the SLA; the working window
is half-open (the open instant counts, the close instant does not); and only
exact durations are accepted — a month of business hours has no meaning.

The property that holds it all together, tested over mixed durations:
`business_hours_between(a, add_business_hours(a, n, cal), cal) = n`.

---

## Where to go next

- `docs/datetime_design.md` — the design, every decision with its reasoning,
  and what is deferred (timezones, business-hours arithmetic,
  observed-holiday shifting — each deferred by decision, not by accident).
- `docs/reference.md` — the datetime/duration sections of the language
  reference.
- `examples/dates_select_test.bas` and friends — the test suite's own
  self-checking examples, which double as a second cookbook.
