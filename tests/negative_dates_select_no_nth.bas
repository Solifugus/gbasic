' A scoped spec without nth is a SPEC error (which Thursday of the month?),
' not a miss -- misses are unknown, malformed specs raise.
program main(args)
    load dates from "../stdlib/dates.bas"
    d {date}= "2026-08-17"
    print dates.select({ weekday: "thursday", within: "month" }, d, dates.calendar({}))
end program
