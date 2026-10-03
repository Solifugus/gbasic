' The same mistake one level in: a series sub-rule is itself a spec, so the
' check recurses into `when:` and names which level the typo is on.
load dates from "../stdlib/dates.bas"
program main(args)
    cal = dates.calendar({})
    jan1 {date}= "2026-01-01"
    s = dates.series({ every: "month", when: { nth: 1, weekdayz: ["monday"] } }, { from: jan1, count: 2 }, cal)
    print(string(count(s)))
end program
