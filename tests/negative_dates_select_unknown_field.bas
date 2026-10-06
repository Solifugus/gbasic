' An unrecognised spec field is a TYPO, and it used to be ignored -- which made
' `{ weekdayz: [...] }` the EMPTY spec, satisfied by every day, so `select`
' answered tomorrow. A constraint silently dropped is worse than one refused,
' because the answer is an ordinary-looking date.
load dates from "../stdlib/dates.bas"
program main(args)
    cal = dates.calendar({})
    anchor {date}= "2026-01-01"
    d = dates.select({ weekdayz: ["monday"] }, anchor, cal)
    print(string(d))
end program
