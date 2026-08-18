# Dates, times, durations, and schedules — Design

Status: **§3, §4, §5, §7 and §8 IMPLEMENTED 2026-08-17 — every planned v1 layer is built.** Remaining: the §9 deferrals (timezones, business-hours arithmetic, observed holidays), each a recorded decision. — §3 behind `examples/datetime_fields_test.bas` (31 self-checks)
with three pinned negatives; §4 — accountant's
month rule, datetime subtraction, duration algebra, and the comparison respec,
behind `examples/datetime_arithmetic_test.bas` (36 self-checks, proven red
first) and three pinned negatives. **Everything else remains proposal.**
Agreed in outline with Matthew 2026-08-17
(dot extraction, accountant's month arithmetic, spec-record selectors, business
calendars as data); the open questions are listed in §10 and nothing below is
implemented until this line says so. Backward compatibility MAY be broken — the
current behaviour this replaces is partly wrong (see §1), so compatibility with
it would mean compatibility with bugs.

The ambition, stated by the project owner: **no programming language does
calendar-aware scheduling well, and gBASIC should be the first.** The target
user is the person planning a convention, a quarterly meeting schedule, a
physician's appointment book, a payroll run — business-calendar work, which is
gBASIC's home ground.

---

## 1. Why a redesign and not a patch

Measured against the current binary (2026-08-17):

| Behaviour | Today | Verdict |
|---|---|---|
| `Jan 31 + 1 month` | `Mar 3` | **Wrong.** Adds "days in starting month". |
| `(90 minutes) > (1 hour)` | `false` | **Broken.** Ordering returns false in *both* directions, always. |
| `(1 month) = (30 days)` and `= (31 days)` | both `true` | **Incoherent.** Equality cannot be true for both. |
| `datetime − datetime` | raises | Missing. |
| `duration + duration`, `duration × n` | raises | Missing. |
| `d.year` | raises | Missing — no component extraction at all. |
| Day-precision `2026-12-25` = `2026-12-25 00:00:00` | `false` | Defensible, but it silently breaks holiday-membership checks (§5). |

Duration comparison is unreliable in every direction, and month arithmetic is
wrong exactly at month-end — the case business code cares about. That is the
ground floor; everything above it (selectors, series, layout) is new.

## 2. The model: one datetime kind, carrying its precision

**Kept, and made load-bearing.** A gBASIC datetime is a calendar value at a
declared precision: `2026` (year), `2026-03` (month), `2026-03-15` (day),
`… 09:30` (minute), `… 09:30:45` (second). One kind; the precision decides how
it renders, and truncation moves between precisions:

```basic
d (date)= "2026-03-15 09:30:45"
m (month)= d          ' 2026-03  — still a datetime, at month precision
```

This replaces the type zoo other languages need (Java's `Year`/`YearMonth`/
`LocalDate`/`LocalDateTime`) with one kind and a lens. It is the most
distinctive thing in the design and the redesign builds on it:

- **Lenses truncate** (produce a coarser datetime).
- **Dot fields extract** (produce numbers) — §3.
- **Equality requires equal precision and equal value.** No cross-precision
  coercion: "equal at the coarser precision" is not transitive (a day equals two
  different second-precision moments that do not equal each other), and a
  non-transitive `=` poisons `contains`/`find`/`consider`. Cross-precision
  comparison is done by truncating explicitly, and the calendar functions do it
  internally so users rarely need to (§5).

Time of day needs **no new kind**: it is an exact duration since midnight — §6.
A **date literal is deliberately NOT added**: `d = 2026-03-15` already parses as arithmetic
(`2026 − 03 − 15` = `2008`), so the literal would change the meaning of an
existing expression, and the modifier form `(date)=` is established. (The
arithmetic reading is a silent trap worth an UNLEARN entry regardless.)

## 3. Component extraction: dot fields — IMPLEMENTED 2026-08-17

Numbers out of a datetime, no new global names:

```basic
d.year        ' 2026
d.month       ' 3          (1-based)
d.day         ' 15
d.hour  d.minute  d.second
d.weekday     ' 1..7, Monday = 1 (ISO)
d.dayname     ' "Sunday"
d.day_of_year ' 74
d.precision   ' "day" — which fields below this are meaningful
```

Reading a field finer than the value's precision yields `unknown` (a month
value has no `.day`), consistent with unknown-as-absent-information elsewhere.

Durations get components too: `dur.years dur.months dur.weeks dur.days
dur.hours dur.minutes dur.seconds` (as stored), plus `dur.total_seconds`, which
**raises if the duration carries months or years** — those have no fixed length,
and a plausible wrong number is worse than a refusal.

## 4. Arithmetic, respecified

### 4.1 datetime + duration — the accountant's rule

Calendar parts first, then exact parts, with **clamping**:

1. add years, then months, to the year/month fields;
2. **clamp the day** to the last valid day of the resulting month;
3. then add weeks/days/hours/minutes/seconds as exact elapsed time.

```basic
Jan 31 + 1 month           ' Feb 28  (clamped)
Jan 31 + 1 month + 1 day   ' Mar 1   (clamp, THEN the exact day)
Feb 29 + 1 year            ' Feb 28
Mar 31 + 1 month           ' Apr 30
```

The order is normative and documented; it is the one Java, dateutil and JS
Temporal converged on independently.

### 4.2 Duration algebra

Durations become **signed** and closed under the obvious operations:

```basic
(1 hour) + (30 minutes)    ' 1 hour 30 minutes
(2 hours) - (30 minutes)   ' 1 hour 30 minutes
(45 minutes) * 4           ' 3 hours
(1 day) / 2                ' 12 hours
b - a                      ' datetime − datetime → exact duration
```

`datetime − datetime` yields an **exact** duration (days/hours/minutes/
seconds), never months — "how many months apart" is a calendar question with a
calendar answer, served by `dates.between(a, b, "months")` in stdlib (which is
also how ages are computed).

Scaling a month/year-bearing duration by a non-integer raises (`2.5 months` has
no meaning); scaling exact parts is fine.

### 4.3 Duration comparison — exact vs calendar, never blurred

A duration has an exact part (weeks/days/hours/minutes/seconds — exact because
gBASIC has no timezone, so a day is 86 400 s) and a calendar part (years,
months). The rules:

- **Ordering** (`<` `>` `<=` `>=`): both operands must be purely exact;
  compared by total seconds. If either carries months/years → **raise**:
  `a month has no fixed length; compare exact durations, or use dates.between`.
  `25 hours > 1 day` is true; `1 month > 30 days` is a refusal, not a guess.
- **Equality**: compare the pair (total months, total exact seconds), with
  canonicalisation `1 year = 12 months`, `1 week = 7 days`. So
  `90 minutes = 1 hour 30 minutes` (true), `1 year = 12 months` (true),
  `1 month = 30 days` (**false** — breaking change, currently true, and
  currently *also* true for 31 days, which is the tell that today's answer is
  noise).

## 5. Business calendars: data, not configuration — IMPLEMENTED 2026-08-17

A calendar is an ordinary record the caller builds and passes — two teams can
hold different calendars in one program, and holidays are data you can load
from a file or a database:

```basic
cal = {
    weekend:  ["saturday", "sunday"],
    holidays: [ h1, h2, h3 ],          ' day-precision datetimes
    hours:    { open: "9:00", close: "17:00" }  ' used by schedule.bas (§8)
}
```

Hours are `open`/`close`, **not** `start`/`end` — changed at implementation
time: `end` is a keyword that can be a record-literal key but cannot follow a
dot, so `cal.hours.end` would be a parse error (the same trap consolidate.bas
hit with `as`; see DOGFOOD 2026-08-15).

gBASIC functions have fixed arity, so the "default calendar" lives in the
constructor rather than an optional parameter — `dates.calendar({})` is
Sat/Sun weekend with no holidays, and the constructor normalises supplied
holidays to day precision once, so membership never hits the §2 precision
rule. Core surface (stdlib `dates.bas`, all taking `cal`):

```basic
cal = dates.calendar(spec)
dates.is_business_day(d, cal)
dates.next_business_day(d, cal)          ' strictly after d
dates.previous_business_day(d, cal)
dates.add_business_days(d, n, cal)       ' n may be negative
dates.business_days_between(a, b, cal)
```

Membership against `holidays` is computed at **day precision internally** —
the library truncates before comparing, so the §2 precision rule never bites
the user here. (Manually, the idiom is `x (day)= session_start` then
`contains(cal.holidays, x)`.)

**Roll conventions**, because "lands on a weekend" is the normal case, not the
edge: `roll: "forward"` (next business day), `"backward"`, or `"modified"`
(forward unless that crosses into the next month, then backward — the finance
standard). Selectors and series accept `roll:`; payment-date code needs it on
day one.

**Merging calendars** (Matthew's addition, 2026-08-17): finding mutual meeting
days needs no new search machinery, because combining calendars is a *union of
constraints* — and the merged result composes with every verb already designed:

```basic
both = dates.merge([cal_alice, cal_bob])
' weekend: union · holidays: union · hours: latest open, earliest close
day = dates.next_business_day(d, both)          ' first mutual working day
slots = schedule.slots(day, spec, both)         ' mutual free slots that day
```

with the law that makes it testable by arithmetic:
`is_business_day(d, merge([a, b])) = is_business_day(d, a) and
is_business_day(d, b)`. A merge can produce an empty working window (one
calendar ends before the other begins); the consumers already handle that —
`slots` yields none, `layout` reports everything unplaced — so `merge` itself
never raises. Intra-day busy-time (existing bookings) stays application state
in v1; it folds in later with business-hours arithmetic (§9).

Deferred, recorded so they are decisions rather than gaps: observed-holiday
shifting (Christmas-on-Saturday → Friday off), half days, per-weekday hours.

## 6. Time of day — an exact duration, not a new kind

**Resolved 2026-08-17 (Matthew): no `9:00` literal, and no `time` kind.**

The literal fails the bigger picture: dates cannot be literals (`2026-12-25`
already parses as `2026 − 12 − 25`), so bare times beside quoted dates would be
incoherent, and the combined `2026-12-25 09:00` could not exist either way. It
does not need to: `(date)= "2026-12-25 09:30"` already parses, at minute
precision — the string form accepts every ISO prefix (`"2026"`, `"2026-03"`,
`"2026-03-15"`, `"… 09:30"`, `"… 09:30:45"`), measured.

A dedicated `time` kind also fails the earn-it test, because time-of-day
already has a faithful representation: **an exact duration since midnight**.
`9 hours 30 minutes` is 09:30, arithmetically and honestly — `day + 9 hours
30 minutes` works today, durations order and compare (§4.3), and they sit in
records. So:

- `d.time` → exact duration since midnight (`9 hours 30 minutes`).
- `dates.at(day, t)` → full datetime; `t` may be a duration **or** a string
  (`"9:30"`), which the library parses.
- In spec and calendar records, times are **strings** — `at: "14:00"`,
  `hours: { start: "9:00", end: "17:00" }` — exactly as weekdays already are
  (`weekday: "thursday"` is a string the library interprets). Consistent,
  and the records stay serialisable data.

One syntax explicitly rejected: `{ start(time): "9:00" }`. The `name (word):`
slot in a record literal is **owned by PBI** — it errors today with `unknown
field policy (expected copy, link, reset, or exclude)`, pinned by
`negative_pbi_unknown_policy` — and opening that closed set to general
modifiers would put two unrelated mechanisms in one grammar position.

## 7. One vocabulary, three verbs — IMPLEMENTED 2026-08-17

Two field names changed at implementation time, both for the keyword-after-dot
rule that already renamed `hours.end`: the series sub-rule is **`when:`**
(`spec.on` is a parse error — `on` is a keyword) and series bounds are
**`{ from:, through: }` or `{ from:, count: }`** (`bounds.to` is a parse
error, and `through` is honest about being inclusive). A malformed spec
ERRORS; a spec no day satisfies yields UNKNOWN — the two failure modes mean
different things.

Everything Matthew listed — third Thursday, first Tuesday after the 15th, last
Wednesday of the month, first business day before a deadline — is one question
family: *which day satisfies these constraints, relative to this anchor?* The
design gives it **one spec vocabulary** (a record, following `grid.extract` and
`consolidate.merge`) **shared by three verbs**:

```basic
dates.matches(d, spec, cal)          ' boolean: does d satisfy spec?
dates.select(spec, anchor, cal)      ' the ONE day: datetime, or unknown
dates.series(spec, bounds, cal)      ' ALL of them: array of datetimes
```

Spec fields (v1, deliberately small):

| Field | Meaning |
|---|---|
| `weekday:` | `"thursday"`, or a list |
| `nth:` | 1, 2, 3 … from the start; −1, −2 from the end; `"last"` = −1 |
| `day:` | day of month (with `roll:` for short months: `day: 31, roll: "backward"`) |
| `kind:` | `"business"` — constrains to business days under `cal` |
| `within:` | `"month"` \| `"quarter"` \| `"year"` \| `"week"` — scope for `nth`, taken from the anchor |
| `after:` / `on_or_after:` / `before:` / `on_or_before:` | anchor constraints — **strictness is in the name**, because the `(next friday)`-from-a-Friday trap must not be reproduced |
| `at:` | a time of day to stamp on the result — a string (`"14:00"`) or an exact duration |
| `every:` | series only — `"day"`, `"week"`, `"month"`, `"quarter"`, `"year"`, or a duration (`2 weeks` for payroll) |
| `except:` | days to skip (a list, or `cal.holidays`) |
| `roll:` | what to do when the computed day is not a business day |

Worked, against Matthew's own scenarios:

```basic
' third Thursday of the month containing d
dates.select({ nth: 3, weekday: "thursday", within: "month" }, d)

' first Tuesday after the 15th
dates.select({ nth: 1, weekday: "tuesday", after: { day: 15 } }, d)

' last Wednesday of the month
dates.select({ nth: "last", weekday: "wednesday", within: "month" }, d)

' first available business day before a deadline
dates.select({ nth: 1, kind: "business", before: deadline }, d, cal)

' board meets every third Thursday at 14:00, all year
meetings = dates.series(
    { every: "month", when: { nth: 3, weekday: "thursday" }, at: "14:00" },
    { from: jan1, through: dec31 }, cal)

' payroll: every 2 weeks from an anchor payday, rolled OFF weekends/holidays
paydays = dates.series(
    { every: 2 weeks, roll: "backward" },
    { from: first_payday, count: 26 }, cal)   ' steps are start + step*k,
                                              ' never cumulative
```

`select` returns **`unknown`** when nothing satisfies the spec (a fifth Tuesday,
a business day before Monday in a holiday week) — the established gBASIC answer
for absent information, and kinder than raising in scheduling code that probes.

Why records and not a string mini-language ("2nd tue after 15th"): no grammar
to learn or misparse, `unknown field` errors instead of parse errors,
composable (a spec can be built up in code), and it is the house pattern.
iCalendar's RRULE is the cautionary tale — a string vocabulary that grew until
nobody could write it unaided.

Why records and not datejs-style chaining (`Date.today().next().friday()`),
considered 2026-08-17: **a chain is code; a spec is data.** The target systems
— physician scheduling, recurring meetings, company calendars — keep their
recurrence rules in a database or a config file, build them from code or a UI,
and inspect them when something goes wrong. A record does all of that natively
(`encode` it, store it, `send` it to an actor, print it); a chain exists only
in source, written in advance. A half-built chain (`.next()` of nothing) is a
runtime state with no meaning, where a bad spec fails as `unknown field` with
the whole record printable. And one vocabulary serves all three verbs, where a
fluent surface would be rebuilt per verb. Chaining's real virtue — readability
for one-offs — is what the lenses already provide: `(next friday)=` and
`(end of month)=` are the fluent form, gBASIC-flavoured, and stay as sugar over
this engine.

## 8. Scheduling: `stdlib/schedule.bas` — IMPLEMENTED 2026-08-17

Its own library (loading `dates`), because packing events is not date
arithmetic. Two entry points:

**`schedule.slots(day, spec, cal)`** — cut a day into appointment slots.
Physician scenario directly:

```basic
slots = schedule.slots(day, { length: 20 minutes, gap: 10 minutes }, cal)
' -> [ { starts:, ends: }, ... ] within cal.hours, skipping nothing booked yet;
'    booking state is the application's, not the library's
```

**`schedule.layout(plan, days, cal)`** — pack ordered sessions into business
days. The convention scenario:

```basic
plan = {
    gap:    15 minutes,
    breaks: [ { at: "12:00", length: 1 hour, name: "Lunch" } ],
    sessions: [
        { name: "Opening keynote", length: 90 minutes },
        { name: "Workshop A",      length: 2 hours },
        { name: "Panel",           length: 45 minutes },
        ...
    ]
}
sched = schedule.layout(plan, dates.series({ every: "business day" },
                                           { from: start, count: 3 }, cal), cal)
' -> { scheduled: [ { name:, starts:, ends:, day: } ... ], unplaced: [names] }
```

Layout rules, decided rather than discovered: sessions keep their order; a
session that does not fit before day end **moves whole** to the next day (never
split silently); anything that fits nowhere is reported in a `unplaced:` list
rather than dropped. Breaks are immovable; gaps are not inserted around them
twice.

## 9. What this deliberately does not do (v1)

- **Timezones.** Everything is naive local time. A decision, not a drift:
  retrofitting zones is painful, but the target domain (one organisation's
  calendar) mostly lives in one zone, and zones would double the design. The
  door left open: the datetime representation must not preclude a zone field
  later.
- **Business-hours arithmetic** ("respond within 4 business hours") — wants
  `cal.hours` interval math; natural v2 on top of §5+§6.
- **Multi-resource scheduling** (rooms, staff) — application logic over
  `slots`; revisit if a pattern hardens.
- **Leap seconds, non-Gregorian calendars.** Out.

## 10. Open questions for Matthew

1. ~~The `9:00` time literal~~ — **resolved, rejected** (Matthew,
   2026-08-17): dates cannot be literals, so a time-only literal is
   incoherent in the bigger picture. See §6 for the resolution.
2. ~~Calendar: separate argument or spec field?~~ — **resolved: separate
   argument** (Matthew, 2026-08-17). One spec reused against two calendars is
   the realistic case.
3. ~~Signed durations~~ — **resolved: signed** (Matthew, 2026-08-17): "the
   honest algebra is right." Implemented; `b - a` for a later `b` yields a
   negative exact duration, rendered with per-component signs.
4. ~~`d.weekday` numbering~~ — **resolved: ISO 8601, Monday=1 … Sunday=7**
   (Matthew, 2026-08-17). Fact check recorded because it came up: ISO has no
   zero — Sunday=0 is the C/JavaScript convention, Sunday=1 is Excel's. ISO's
   numbering makes `d.weekday <= 5` the workday test, which suits the domain.

## 11. Test strategy (before any code)

Fixture-first, like messy.xlsx: each scenario is an example whose golden is
checkable by arithmetic, not transcript:

- **convention layout** — total scheduled time must equal the sum of session
  lengths; no session crosses a break, a day end, or a holiday; order preserved.
- **quarterly meetings** — every generated date must satisfy
  `dates.matches`, land on a business day, and there must be exactly N.
- **payroll** — 26 paydays, none on a weekend/holiday, gaps of exactly 14 days
  except where rolled — and the roll never moves a payday across a month
  boundary when `modified` is chosen.
- **physician slots** — slot count × length + gaps fills `cal.hours` exactly;
  no slot overlaps a break.
- **month-end table** — the §4.1 examples pinned exactly; plus the round-trip
  property `(d + 1 month) - 1 month = d` documented as NOT holding at month-end
  (clamping is lossy), so nobody "fixes" it later.
- **calendar merge law** — `is_business_day(d, merge([a,b]))` equals the
  conjunction, property-checked over generated calendars and days.
- **duration law checks** — ordering is a total order on exact durations;
  equality is transitive (property-tested over generated values, the
  PLAT-NUMFMT pattern); the removed behaviours (`1 month = 30 days`) pinned as
  negatives.
