' A calendar where every day is weekend -- which dates.merge can legitimately
' produce -- must make a lookup FAIL, not hang. The guard is ~10 years of days.
program main(args)
    load dates from "../stdlib/dates.bas"
    dead = dates.calendar({ weekend: ["monday","tuesday","wednesday","thursday","friday","saturday","sunday"] })
    d {date}= "2026-08-17"
    print dates.next_business_day(d, dead)
end program
