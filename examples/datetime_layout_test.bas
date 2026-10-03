' A DATE LAYOUT, WRITTEN THE WAY IT READS: `{string "YYYY-MM-DD hh:mm:ss"}d`
' rather than `%Y-%m-%d %H:%M:%S`.
'
' NOT A NEW VERB. Every modifier here is named for what it PRODUCES -- {date},
' {number}, {USD}, {trimmed} -- and `{string}` already turned a datetime into
' text, so a layout is an ARGUMENT to it. `{format "..."}` was the first
' proposal and Matthew rejected it on exactly that ground: the name says nothing
' about the result, and `{format "..."}d` reads as though text goes in.
'
' ONE RULE CARRIES THE NOTATION: date parts are UPPERCASE, time parts are
' lowercase. That resolves the collision every other scheme fumbles -- `MM` is
' the month, `mm` is the minutes -- and it is memorable, where `%M` against `%m`
' is a coin flip you look up every time.
'
' A SECOND RULE COVERS THE NAMES and is the same for both: one or two letters is
' a NUMBER, three is a SHORT NAME, four is a LONG NAME.
'
' SELF-CHECKING AND FORCED. Every defect here is a PLAUSIBLE DATE STRING: a
' 12-hour clock off by one at midnight prints `0:00 am`, a weekday computed with
' the wrong branch prints the name of a real day, and a golden would record
' either and defend it.

function check(label, got, want)
    if string(got) = string(want) then
        print "ok   " + label
    else
        print "MISMATCH " + label + ": got " + string(got) + ", want " + string(want)
    end if
    return nothing
end function

program main( args )
    d {datetime}= "2026-03-07 14:05:09"       ' a Saturday

    print "-- the common layouts"
    check("ISO date             ", {string "YYYY-MM-DD"}d, "2026-03-07")
    check("ISO date and time    ", {string "YYYY-MM-DD hh:mm:ss"}d, "2026-03-07 14:05:09")
    check("and it equals {string}", {string "YYYY-MM-DD hh:mm:ss"}d, {string}d)
    check("day first            ", {string "DD/MM/YYYY"}d, "07/03/2026")
    check("month first          ", {string "M/D/YY"}d, "3/7/26")
    check("no separators at all ", {string "YYYYMMDD"}d, "20260307")

    print ""
    print "-- 1 or 2 letters a NUMBER, 3 a SHORT name, 4 a LONG name"
    check("M  the month number  ", {string "M"}d, "3")
    check("MM zero-padded       ", {string "MM"}d, "03")
    check("MMM  short           ", {string "MMM"}d, "Mar")
    check("MMMM long            ", {string "MMMM"}d, "March")
    check("D  the day number    ", {string "D"}d, "7")
    check("DD zero-padded       ", {string "DD"}d, "07")
    check("DDD  short day name  ", {string "DDD"}d, "Sat")
    check("DDDD long day name   ", {string "DDDD"}d, "Saturday")
    check("YY two digits        ", {string "YY"}d, "26")
    check("a sentence of them   ", {string "DDDD, D MMMM YYYY"}d, "Saturday, 7 March 2026")

    print ""
    print "-- weekdays against an OUTSIDE ORACLE"
    ' Sakamoto's method has a BRANCH -- `if m < 3 then y = y - 1` -- that a
    ' March-only test never takes, and the first version of this had only March.
    ' These names were taken from coreutils `date -d`, which knows nothing about
    ' this arithmetic: January and February exercise the branch, 2024 and 2000
    ' are leap years, 1900 is the century that is NOT, and 1970 is the epoch.
    jan {date}= "2026-01-01"
    check("2026-01-01 (branch)  ", {string "DDDD"}jan, "Thursday")
    feb {date}= "2026-02-01"
    check("2026-02-01 (branch)  ", {string "DDDD"}feb, "Sunday")
    leap {date}= "2024-02-29"
    check("2024-02-29 leap      ", {string "DDDD"}leap, "Thursday")
    cent {date}= "2000-02-29"
    check("2000-02-29 century   ", {string "DDDD"}cent, "Tuesday")
    nonleap {date}= "1900-03-01"
    check("1900-03-01 non-leap  ", {string "DDDD"}nonleap, "Thursday")
    epoch {date}= "1970-01-01"
    check("1970-01-01 epoch     ", {string "DDDD"}epoch, "Thursday")
    eoy {date}= "2026-12-31"
    check("2026-12-31           ", {string "DDDD"}eoy, "Thursday")

    print ""
    print "-- THE 12-HOUR CLOCK IS IMPLICIT: am/pm in the layout switches it"
    check("24-hour by default   ", {string "hh:mm"}d, "14:05")
    check("12-hour with pm      ", {string "h:mm pm"}d, "2:05 pm")
    check("the CASE you wrote   ", {string "h:mm PM"}d, "2:05 PM")
    ' THE BOUNDARIES, where this family is classically off by one: midnight must
    ' be 12 am and not 0 am, and noon must be 12 pm and not 0 pm.
    mid {datetime}= "2026-03-07 00:00:00"
    check("midnight is 12 am    ", {string "h:mm pm"}mid, "12:00 am")
    noon {datetime}= "2026-03-07 12:00:00"
    check("noon is 12 pm        ", {string "h:mm pm"}noon, "12:00 pm")
    bnoon {datetime}= "2026-03-07 11:59:00"
    check("11:59 is am          ", {string "h:mm pm"}bnoon, "11:59 am")
    late {datetime}= "2026-03-07 23:59:00"
    check("23:59 is 11:59 pm    ", {string "h:mm pm"}late, "11:59 pm")

    print ""
    print "-- what the value does NOT carry"
    ' A date-precision value really IS midnight, so rendering a time is honest
    ' rather than invented.
    dateonly {date}= "2026-03-07"
    check("a date has midnight  ", {string "YYYY-MM-DD hh:mm:ss"}dateonly, "2026-03-07 00:00:00")
    ' A time-only value has no date at all, so a date token is refused rather
    ' than answered with a zero that looks like a year.
    t {time}= "14:05:09"
    check("a time renders a time", {string "hh:mm:ss"}t, "14:05:09")
    on error goto next
    bad = {string "YYYY"}t
    check("but not a date token ", contains(error.message, "needs a date"), true)
    error.clear()

    print ""
    print "-- refusals, each beside a legal neighbour"
    ' AN UNKNOWN LETTER RUN IS REFUSED BY NAME, which is what lets prose stay
    ' OUTSIDE the layout. Measured against the system word list: 4,536 of
    ' 104,334 English words contain `ss` and 939 contain `mm`, so if prose
    ' passed through, "Business hours: hh:mm" would render the `ss` in
    ' "Business" as seconds -- about one word in twenty-three corrupted, and
    ' they are the words a caption uses.
    nope = {string "ZZZ"}d
    check("an unknown token     ", contains(error.message, "not a date layout token"), true)
    check("and it NAMES the run ", contains(error.message, "ZZZ"), true)
    error.clear()
    word = {string "Business hh:mm"}d
    check("prose is refused     ", contains(error.message, "Business"), true)
    error.clear()
    three = {string "YYY"}d
    check("YYY means nothing    ", contains(error.message, "YYY"), true)
    error.clear()
    num = {string "YYYY"}42
    check("a non-date subject   ", contains(error.message, "applies to a date"), true)
    error.clear()
    two = {string "YYYY", "MM"}d
    check("two layouts          ", contains(error.message, "takes ONE layout"), true)
    error.clear()
    ' CONTROL: the no-argument form is untouched, in both positions.
    plain {string}= d
    check("CONTROL {string}d    ", plain, "2026-03-07 14:05:09")
    check("CONTROL on a number  ", {string}42, "42")
    ' CONTROL: prose OUTSIDE the layout is how you write a caption.
    check("CONTROL prose outside", "Posted " + {string "D MMM YYYY"}d, "Posted 7 Mar 2026")
    ' CONTROL: a layout chains, which is what the clause work bought.
    raw = "  2026-03-07 14:05:09  "
    check("CONTROL in a chain   ", {trimmed; datetime; string "D MMM YYYY"}raw, "7 Mar 2026")
end program
