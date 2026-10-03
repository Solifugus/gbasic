' A calendar spec is a SEPARATE vocabulary and gets the same rule: `holidayz`
' built a calendar with no holidays in it, so every business-day question
' answered as though the office never closed.
load dates from "../stdlib/dates.bas"
program main(args)
    cal = dates.calendar({ holidayz: [{date}"2026-07-04"] })
    print(string(count(cal.holidays)))
end program
