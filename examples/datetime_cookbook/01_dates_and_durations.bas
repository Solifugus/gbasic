' Recipe 1 — Datetimes carry their precision; durations are real values.
'
' One datetime kind covers what other languages need four types for: a year,
' a month, a date and a timestamp are the SAME kind at different precisions,
' and the precision decides how the value renders. There are no date literals
' (2026-12-25 would parse as subtraction!) -- the {date} modifier takes an
' ISO string at any precision.

program main(args)
    y {date}= "2026"
    m {date}= "2026-03"
    d {date}= "2026-03-15"
    t {date}= "2026-03-15 09:30"

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
    jan31 {date}= "2026-01-31"
    print ""
    print "Jan 31 + 1 month  = " + (jan31 + 1 month)
    print "Jan 31 + 3 months = " + (jan31 + (1 month) * 3)
    feb29 {date}= "2024-02-29"
    print "Feb 29 + 1 year   = " + (feb29 + 1 year)
    print "back off month-end: " + (jan31 + 1 month - 1 day)
end program
