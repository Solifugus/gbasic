' Business-hours arithmetic without a working window is refused by name.
program main(args)
    load dates from "../stdlib/dates.bas"
    d {date}= "2026-08-17 10:00:00"
    print dates.add_business_hours(d, 1 hour, dates.calendar({}))
end program
