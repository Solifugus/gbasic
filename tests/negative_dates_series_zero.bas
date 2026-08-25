' A zero step can never advance; refused rather than emitting one day 10000x.
program main(args)
    load dates from "../stdlib/dates.bas"
    d {date}= "2026-08-17"
    print dates.series({ every: 0 days }, { from: d, count: 3 }, dates.calendar({}))
end program
