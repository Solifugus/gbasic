' {date}, {time} and {datetime} given a datetime VALUE convert it
' (docs/datetime_conversion_design.md §2). Run by tests/run_datetime_convert.sh.
'
' SELF-CHECKING: every answer is written here by hand. Each is checked three
' ways -- its text, its precision and, as an oracle the conversion does not
' share, its fields against the fields of the value it came from -- because
' two wrong conversions could print alike.

checks = { n: 0, bad: 0 }

function check(label, got, want)
    checks.n = checks.n + 1
    if type(got) = type(want) then
        if got = want then
            return nothing
        end if
    end if
    checks.bad = checks.bad + 1
    print "MISMATCH " + label + ": got " + type(got) + " " + string(got) + ", want " + type(want) + " " + string(want)
    return nothing
end function

' a modifier applied by name, its refusal caught
function convert(v, m)
    on error goto next
    if m = "date" then
        r {date}= v
    else
        if m = "time" then
            r {time}= v
        else
            r {datetime}= v
        end if
    end if
    if error then
        return { refused: error.message }
    end if
    return { value: r }
end function

function expect(label, v, m, text, precision)
    got = convert(v, m)
    if has(got, "refused") then
        check(label + " {" + m + "}", "refused: " + got.refused, text)
        return nothing
    end if
    check(label + " {" + m + "} text", string(got.value), text)
    check(label + " {" + m + "} precision", string(got.value.precision), precision)
    return nothing
end function

function refuses(label, v, m, phrase)
    got = convert(v, m)
    if has(got, "refused") then
        check(label + " {" + m + "} refusal names what is missing", contains(got.refused, phrase), true)
    else
        check(label + " {" + m + "}", "converted to " + string(got.value), "refused: " + phrase)
    end if
    return nothing
end function

sec {datetime}= "2026-01-05 10:30:45"
mi {minute}= sec
hr {hour}= sec
dy {day}= sec
mo {month}= sec
yr {year}= sec
tsec {time}= "10:30:45"
tmin {time}= "10:30"

' ---- {date}: the value's date -----------------------------------------
expect("second", sec, "date", "2026-01-05", "day")
expect("minute", mi, "date", "2026-01-05", "day")
expect("hour", hr, "date", "2026-01-05", "day")
expect("day", dy, "date", "2026-01-05", "day")
refuses("month", mo, "date", "a year or a month has no day to take")
refuses("year", yr, "date", "a year or a month has no day to take")
refuses("time", tsec, "date", "a time of day has no date to take")

' ---- {time}: its time of day, at its own precision ------------------------
expect("second", sec, "time", "10:30:45", "time")
expect("minute", mi, "time", "10:30", "time")
expect("hour", hr, "time", "10", "time")   ' as the text "10" reads and prints
expect("time", tsec, "time", "10:30:45", "time")
expect("time of minutes", tmin, "time", "10:30", "time")
refuses("day", dy, "time", "a date has no time of day to take")
refuses("month", mo, "time", "a year or a month has no time of day to take")

' ---- {datetime}: to the second, a date at midnight ------------------------
expect("second", sec, "datetime", "2026-01-05 10:30:45", "second")
expect("minute", mi, "datetime", "2026-01-05 10:30:00", "second")
expect("hour", hr, "datetime", "2026-01-05 10:00:00", "second")
expect("day", dy, "datetime", "2026-01-05 00:00:00", "second")
refuses("month", mo, "datetime", "a year or a month has no day to take")
refuses("time", tsec, "datetime", "a time of day has no date to take")

' ---- the oracle: fields against the source's --------------------------
d = convert(sec, "date").value
check("{date} keeps the year", d.year, sec.year)
check("{date} keeps the month", d.month, sec.month)
check("{date} keeps the day", d.day, sec.day)
check("{date}'s result equals the same date written as text", d = dy, true)
t = convert(sec, "time").value
check("{time} keeps the hour", t.hour, sec.hour)
check("{time} keeps the minute", t.minute, sec.minute)
check("{time} keeps the second", t.second, sec.second)
check("{time}'s result equals the same time written as text", t = tsec, true)
' `=` on two times ignores their date fields, so a time that kept its old date
' would pass the line above; serialize writes every field, and is what G-SQL
' stores and keys, so a leftover date would make two equal times two keys
check("{time}'s result is the same bytes as the text time", serialize(t) = serialize(tsec), true)
x = convert(dy, "datetime").value
check("{datetime} of a date keeps its day", x.day, dy.day)
check("{datetime} of a date is midnight", x.hour * 3600 + x.minute * 60 + x.second, 0)
check("{datetime} of a date equals the text form's midnight", x = convert("2026-01-05", "datetime").value, true)

' ---- the control: the text forms are unchanged ---------------------------
expect("text date", "2026-01-05", "date", "2026-01-05", "day")
expect("text date and time", "2026-01-05 10:30", "date", "2026-01-05 10:30", "minute")
expect("text time", "10:30", "time", "10:30", "time")
expect("text date", "2026-01-05", "datetime", "2026-01-05 00:00:00", "second")
expect("text date and time", "2026-01-05 10:30", "datetime", "2026-01-05 10:30:00", "second")
refuses("text time", "10:30", "date", "expects an ISO-like date string")
refuses("text date", "2026-01-05", "time", "expects an ISO-like time string")
refuses("a number", 5, "date", "expects an ISO-like date string")

' ---- inline, as a program writes it --------------------------------------
check("inline {date}", {date}sec = dy, true)
check("inline {time}", string({time}sec), "10:30:45")

print "ok " + string(checks.n) + " checks, " + string(checks.bad) + " mismatches"
