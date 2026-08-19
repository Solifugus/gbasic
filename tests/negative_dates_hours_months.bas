' A month of business hours has no meaning: months have no fixed length, so
' the request is refused rather than answered with a guess.
program main(args)
    load dates from "../stdlib/dates.bas"
    cal = dates.calendar({ hours: { open: "9:00", close: "17:00" } })
    d (date)= "2026-08-17 10:00:00"
    print dates.add_business_hours(d, 1 month, cal)
end program
