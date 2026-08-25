' Recipe 3 — Dates on the x-axis label as DATES, not day numbers.
'
' A datetime column plots via core duration arithmetic (d - anchor is an
' exact duration since the redesign) and the tick labels come back through
' the datetime renderer, so the axis reads 2026-02-10, never 10 or 46063.

program main(args)
    load chart from "../../stdlib/chart.bas"

    d1 {date}= "2026-01-31"
    trend = {
        day: [d1, d1 + 14 days, d1 + 28 days, d1 + 42 days],
        users: [120, 180, 260, 410]
    }
    svg = chart.line(trend, "day", "users")

    print "labels are dates: " + string(contains(svg, ">2026-02-10</text>"))
    print string(len(svg)) + " chars"
end program
