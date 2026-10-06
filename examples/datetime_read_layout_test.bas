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
    ' THIS CHECK USED TO ASSERT "does not fit", WHICH IS FALSE ABOUT ITS OWN
    ' INPUT: `Sunday, 7 March 2026` fits `DDDD, D MMMM YYYY` exactly, and what
    ' is wrong is the CALENDAR -- that date is a Saturday. Corrected with the
    ' rest of them 2026-10-03.
    check("a contradictory day  ", contains(error.message, "was a Saturday, not a Sunday"), true)
    error.clear()
    ' The message names the TEXT and EVERY layout tried, because "could not
    ' parse" alone cannot tell a reader whether the data is wrong or the list is
    ' short.
    none = {date "YYYY-MM-DD", "DD/MM/YYYY"}"7 Mar 2026"
    check("names the text       ", contains(error.message, "7 Mar 2026"), true)
    check("and both layouts     ", contains(error.message, "DD/MM/YYYY"), true)
    error.clear()
    over = {date "DD/MM/YYYY"}"32/01/2026"
    ' Same correction: `32/01/2026` fits `DD/MM/YYYY` -- two digits, two
    ' digits, four -- and January simply has 31 days.
    check("day 32               ", contains(error.message, "January 2026 has 31 days"), true)
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
    ' AND THE MESSAGE IS PART OF THE ASSERTION, which these four checks did not
    ' say when they were written: they asserted the word "ISO-like", so they
    ' pinned a sentence that is FALSE about their own input. `2026-02-30` IS
    ' ISO-like -- that is the one thing about it which is not wrong -- and the
    ' author was sent to look at the shape of a string with nothing wrong with
    ' its shape. Fixed 2026-10-03, found verifying the 0.5.0 artifact by
    ' running it; the checks now name the cause, which is what a reader needs.
    feb29 = {date}"2026-02-29"
    check("29 Feb in a non-leap ", error.message,
          "`2026-02-29` is not a real date -- 2026 was not a leap year, so February 2026 has 28 days")
    error.clear()
    feb30 = {date}"2026-02-30"
    check("30 Feb ever          ", error.message,
          "`2026-02-30` is not a real date -- February 2026 has 28 days")
    error.clear()
    apr31 = {date}"2026-04-31"
    check("31 April             ", error.message,
          "`2026-04-31` is not a real date -- April 2026 has 30 days")
    error.clear()
    jun31 = {date "YYYY-MM-DD"}"2026-06-31"
    check("31 June via a layout ", error.message,
          "`2026-06-31` is not a real date -- June 2026 has 30 days")
    error.clear()
    ' THE LEAP RULE IN FULL, which is where a half-done fix shows: 2024 is a
    ' leap year, 2000 is one BECAUSE it divides by 400, and 1900 is NOT because
    ' it is a century that does not.
    ok24 {date}= "2024-02-29"
    check("CONTROL 2024 leap    ", ok24, "2024-02-29")
    ok00 {date}= "2000-02-29"
    check("CONTROL 2000 leap    ", ok00, "2000-02-29")
    no1900 = {date}"1900-02-29"
    check("1900 is NOT leap     ", error.message,
          "`1900-02-29` is not a real date -- 1900 was not a leap year, so February 1900 has 28 days")
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

    print ""
    print "-- THE REFUSAL NAMES THE CAUSE, AND ONLY WHEN THERE IS ONE TO NAME"
    ' THE CONTROL IS THE LOAD-BEARING HALF OF THIS WHOLE SECTION. "Name the
    ' cause" is satisfied by a build that blames the CALENDAR for every string
    ' that will not parse -- which is the same error one direction over, and
    ' would be worse, since it would answer a question about February for input
    ' that is not a date at all. So a malformed string must STILL get the
    ' generic sentence, and that is asserted first.
    bad = {date}"not a date"
    check("CONTROL malformed    ", error.message,
          "date modifier expects an ISO-like date string")
    error.clear()
    bad2 = {time}"half past four"
    check("CONTROL malformed time", error.message,
          "time modifier expects an ISO-like time string")
    error.clear()
    bad3 = {date "YYYY-MM-DD"}"not a date at all"
    check("CONTROL layout no fit", contains(error.message, "does not fit any of the layouts"), true)
    error.clear()

    ' EVERY CALENDAR RULE HAS ITS OWN SENTENCE, because each has a different
    ' remedy and an author cannot act on a rule nobody named.
    m13 = {date}"2026-13-01"
    check("month out of range   ", error.message,
          "`2026-13-01` is not a real date -- a month is 1 to 12, not 13")
    error.clear()
    h25 = {time}"25:00:00"
    check("hour out of range    ", error.message,
          "`25:00:00` is not a real time -- an hour is 0 to 23, not 25")
    error.clear()
    mi60 = {time}"10:60:00"
    check("minute out of range  ", error.message,
          "`10:60:00` is not a real time -- a minute is 0 to 59, not 60")
    error.clear()
    se61 = {time}"10:00:61"
    check("second out of range  ", error.message,
          "`10:00:61` is not a real time -- a second is 0 to 59, not 61")
    error.clear()
    dtbad = {datetime}"2026-02-30 10:00:00"
    check("{datetime} says date-time", error.message,
          "`2026-02-30 10:00:00` is not a real date-time -- February 2026 has 28 days")
    error.clear()

    ' A DAY NAME THAT DOES NOT MATCH is the OTHER way a well-shaped string
    ' fails to be a real date, and "does not fit the layout" sends the author to
    ' the layout, which fitted perfectly.
    wrongday = {date "DDDD, D MMMM YYYY"}"Sunday, 7 March 2026"
    check("wrong day name       ", error.message,
          "`Sunday, 7 March 2026` is not a real date -- 7 March 2026 was a Saturday, not a Sunday")
    error.clear()

    ' WITH SEVERAL LAYOUTS THE READING IS NAMED. `02/30/2026` faults under
    ' `DD/MM/YYYY` with "a month is 1 to 12, not 30", which is true of that
    ' reading and reads like nonsense to an author who never wrote a month 30 --
    ' so the layout that produced it is said out loud. The CONTROL beside it is
    ' that a list which CAN disambiguate still does, since a message about the
    ' first fault must not mean the list stopped trying the rest.
    none = {date "DD/MM/YYYY", "MM/DD/YYYY"}"02/30/2026"
    check("list names the reading", error.message,
          "`02/30/2026` is not a real date under any of the 2 layouts given to `{date}` -- read as \"DD/MM/YYYY\", a month is 1 to 12, not 30")
    error.clear()
    still {date "DD/MM/YYYY", "MM/DD/YYYY"}= "03/15/2026"
    check("CONTROL list still works", still, "2026-03-15")
    ' AND ONE LAYOUT DOES NOT CARRY THE EXTRA CLAUSE, or it would be noise on
    ' the commonest shape.
    one = {date "DD/MM/YYYY"}"30/02/2026"
    check("one layout, no clause", error.message,
          "`30/02/2026` is not a real date -- February 2026 has 28 days")
    error.clear()
end program
