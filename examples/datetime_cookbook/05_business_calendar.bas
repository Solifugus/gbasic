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
    mon {date}= "2026-08-17"
    sat {date}= "2026-08-15"
    print "Monday works : " + dates.is_business_day(mon, cal)
    print "Saturday not : " + dates.is_business_day(sat, cal)

    ' Christmas 2026 is a Friday. From Christmas Eve, the next business day
    ' steps over the holiday AND the weekend.
    xmas_stamp {date}= "2026-12-25 09:30:00"
    hcal = dates.calendar({ holidays: [xmas_stamp] })
    eve {date}= "2026-12-24"
    print "after Dec 24 : " + dates.next_business_day(eve, hcal)

    ' Deadlines in working days, both directions.
    print "5 bdays on   : " + dates.add_business_days(mon, 5, hcal)
    print "1 bday back  : " + dates.add_business_days(mon, 0 - 1, hcal)

    ' Working days until a date: counted over (a, b], signed. The convention
    ' is worth knowing -- half-open intervals are where calendar bugs live.
    fri {date}= "2026-08-21"
    print "Mon..Fri     : " + dates.business_days_between(mon, fri, cal) + " working days"

    ' Observed holidays: July 4, 2026 is a Saturday, so under the US federal
    ' rule the day OFF is Friday July 3. observe: "nearest" computes that at
    ' construction, and every verb downstream simply inherits it.
    july4 {date}= "2026-07-04"
    us = dates.calendar({ holidays: [july4], observe: "nearest" })
    fri3 {date}= "2026-07-03"
    thu2 {date}= "2026-07-02"
    print "Jul 3 off    : " + (not dates.is_business_day(fri3, us))
    print "after Jul 2  : " + dates.next_business_day(thu2, us)
end program
