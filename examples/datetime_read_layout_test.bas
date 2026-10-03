' READING A DATE THROUGH THE SAME LAYOUT: `{date "DD/MM/YYYY"}cell`, and a LIST
' when one shape is not enough.
'
' THE ORDER IS THE DECLARATION, which is what makes first-match-wins honest here
' rather than a race. `03/07/2026` is 7 March or 3 July depending on where the
' report came from -- `ari` invented `using date: dmy` for exactly that -- and
' writing `DD/MM/YYYY` ahead of `MM/DD/YYYY` is the author saying which.
'
' AND A LAYOUT MATCHES ONLY IF IT ALSO YIELDS A VALID DATE, which lets a list
' disambiguate itself where the data allows.
'
' SELF-CHECKING AND FORCED: every defect here is a REAL DATE, just not the right
' one. A list that silently preferred the wrong candidate returns a date six
' months off and a golden would record it.

function check(label, got, want)
    if string(got) = string(want) then
        print "ok   " + label
    else
        print "MISMATCH " + label + ": got " + string(got) + ", want " + string(want)
    end if
    return nothing
end function

program main( args )
    print "-- one layout, each shape the notation can read"
    a {date "DD/MM/YYYY"}= "07/03/2026"
    check("day first            ", a, "2026-03-07")
    b {date "MM/DD/YYYY"}= "07/03/2026"
    check("month first          ", b, "2026-07-03")
    c {date "D MMM YYYY"}= "7 Mar 2026"
    check("a short month name   ", c, "2026-03-07")
    d {date "DDDD, D MMMM YYYY"}= "Saturday, 7 March 2026"
    check("long names, and a day", d, "2026-03-07")
    e {date "YYYYMMDD"}= "20260307"
    check("no separators        ", e, "2026-03-07")
    f {datetime "YYYY-MM-DD hh:mm:ss"}= "2026-03-07 14:05:09"
    check("a full timestamp     ", f, "2026-03-07 14:05:09")
    g {datetime "D MMM YYYY h:mm pm"}= "7 Mar 2026 2:05 pm"
    check("12-hour, pm          ", g, "2026-03-07 14:05:00")
    h {datetime "D MMM YYYY h:mm pm"}= "7 Mar 2026 12:30 am"
    check("12-hour, midnight    ", h, "2026-03-07 00:30:00")
    i {datetime "D MMM YYYY h:mm pm"}= "7 Mar 2026 12:30 pm"
    check("12-hour, noon        ", i, "2026-03-07 12:30:00")
    j {time "hh:mm"}= "14:05"
    check("a time only          ", j, "14:05")

    print ""
    print "-- a two-digit year takes the POSIX pivot: 00-68 this century, 69-99 the last"
    k {date "YY-MM-DD"}= "26-03-07"
    check("26 is 2026           ", k, "2026-03-07")
    l {date "YY-MM-DD"}= "68-03-07"
    check("68 is 2068           ", l, "2068-03-07")
    m {date "YY-MM-DD"}= "69-03-07"
    check("69 is 1969           ", m, "1969-03-07")
    n {date "YY-MM-DD"}= "99-12-31"
    check("99 is 1999           ", n, "1999-12-31")

    print ""
    print "-- A LIST, AND THE ORDER IS THE DECLARATION"
    ' The same text, two orders, two answers -- asserted as a DIFFERENCE,
    ' because either answer alone is just a date.
    o {date "DD/MM/YYYY", "MM/DD/YYYY"}= "07/03/2026"
    check("dmy declared first   ", o, "2026-03-07")
    p {date "MM/DD/YYYY", "DD/MM/YYYY"}= "07/03/2026"
    check("mdy declared first   ", p, "2026-07-03")
    check("and they DIFFER      ", o = p, false)

    print ""
    print "-- and the list disambiguates itself where the data allows"
    ' 15 cannot be a month, so the dmy candidate is skipped rather than
    ' inventing month 15 -- which is what `yields a valid date` buys.
    q {date "DD/MM/YYYY", "MM/DD/YYYY"}= "03/15/2026"
    check("03/15 can only be mdy", q, "2026-03-15")
    r {date "MM/DD/YYYY", "DD/MM/YYYY"}= "15/03/2026"
    check("15/03 can only be dmy", r, "2026-03-15")
    ' Three candidates, and the third is the one that fits.
    s2 {date "YYYY-MM-DD", "DD/MM/YYYY", "D MMM YYYY"}= "7 Mar 2026"
    check("the third candidate  ", s2, "2026-03-07")

    print ""
    print "-- refusals, each beside a legal neighbour"
    on error goto next
    ' A DAY NAME IS CHECKED, NOT IGNORED. 7 March 2026 is a Saturday, so
    ' accepting this would admit contradictory data as if it were fine.
    bad = {date "DDDD, D MMMM YYYY"}"Sunday, 7 March 2026"
    check("a contradictory day  ", contains(error.message, "does not fit"), true)
    error.clear()
    ' The message names the TEXT and EVERY layout tried, because "could not
    ' parse" alone cannot tell a reader whether the data is wrong or the list is
    ' short.
    none = {date "YYYY-MM-DD", "DD/MM/YYYY"}"7 Mar 2026"
    check("names the text       ", contains(error.message, "7 Mar 2026"), true)
    check("and both layouts     ", contains(error.message, "DD/MM/YYYY"), true)
    error.clear()
    over = {date "DD/MM/YYYY"}"32/01/2026"
    check("day 32               ", contains(error.message, "does not fit"), true)
    error.clear()
    tail = {date "DD/MM/YYYY"}"07/03/2026 extra"
    check("trailing text        ", contains(error.message, "does not fit"), true)
    error.clear()
    short = {date "DD/MM/YYYY"}"07/03"
    check("text too short       ", contains(error.message, "does not fit"), true)
    error.clear()
    h13 = {datetime "YYYY-MM-DD hh:mm pm"}"2026-03-07 13:05 pm"
    check("hour 13 with pm      ", contains(error.message, "does not fit"), true)
    error.clear()
    num = {date "YYYY"}42
    check("a non-text subject   ", contains(error.message, "reads TEXT"), true)
    error.clear()

    print ""
    print "-- A DAY THAT DOES NOT EXIST IN THAT MONTH"
    ' PRE-EXISTING AND FOUND BY BUILDING THIS: `valid_date_parts` checked the
    ' day as 1..31 from the day the type was written, so `{date}"2026-02-30"`
    ' was ACCEPTED -- and `+ 1 day` then answered `2026-03-03`, because the
    ' epoch conversion normalises 30 February to 2 March. A date that does not
    ' exist became a DIFFERENT REAL DATE two days later with nothing raised.
    feb29 = {date}"2026-02-29"
    check("29 Feb in a non-leap ", contains(error.message, "ISO-like"), true)
    error.clear()
    feb30 = {date}"2026-02-30"
    check("30 Feb ever          ", contains(error.message, "ISO-like"), true)
    error.clear()
    apr31 = {date}"2026-04-31"
    check("31 April             ", contains(error.message, "ISO-like"), true)
    error.clear()
    jun31 = {date "YYYY-MM-DD"}"2026-06-31"
    check("31 June via a layout ", contains(error.message, "does not fit"), true)
    error.clear()
    ' THE LEAP RULE IN FULL, which is where a half-done fix shows: 2024 is a
    ' leap year, 2000 is one BECAUSE it divides by 400, and 1900 is NOT because
    ' it is a century that does not.
    ok24 {date}= "2024-02-29"
    check("CONTROL 2024 leap    ", ok24, "2024-02-29")
    ok00 {date}= "2000-02-29"
    check("CONTROL 2000 leap    ", ok00, "2000-02-29")
    no1900 = {date}"1900-02-29"
    check("1900 is NOT leap     ", contains(error.message, "ISO-like"), true)
    error.clear()
    ok28 {date}= "2026-02-28"
    check("CONTROL 28 Feb       ", ok28, "2026-02-28")
    ok30 {date}= "2026-04-30"
    check("CONTROL 30 April     ", ok30, "2026-04-30")
    ok31 {date}= "2026-12-31"
    check("CONTROL 31 December  ", ok31, "2026-12-31")

    print ""
    print "-- CONTROLS: the no-argument forms are untouched"
    z1 {date}= "2026-03-07"
    check("{date} ISO           ", z1, "2026-03-07")
    z2 {datetime}= "2026-03-07T14:05:09Z"
    check("{datetime} ISO and Z ", z2, "2026-03-07 14:05:09")
    z3 {time}= "14:05:09"
    check("{time} ISO           ", z3, "14:05:09")
    ' AND THE ROUND TRIP, which is the one check that fails if the reader and
    ' the renderer disagree about what a token means.
    rt {date "D MMM YYYY"}= "7 Mar 2026"
    check("read then render     ", {string "D MMM YYYY"}rt, "7 Mar 2026")
    rt2 {datetime "DDDD, D MMMM YYYY h:mm pm"}= "Saturday, 7 March 2026 2:05 pm"
    check("and the long way     ", {string "DDDD, D MMMM YYYY h:mm pm"}rt2, "Saturday, 7 March 2026 2:05 pm")
end program
