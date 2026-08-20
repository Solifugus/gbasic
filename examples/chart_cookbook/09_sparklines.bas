' Recipe 9 — Sparklines: the shape alone, small enough for a table cell.
'
' No axes, no labels, no margins -- a 120x24 line whose only job is to show
' the trend inline. Gaps behave exactly as they do everywhere else.

program main(args)
    load chart from "../../stdlib/chart.bas"

    week = [3, 1, 4, unknown, 5, 9, 2]
    svg = chart.sparkline(week)
    print svg
end program
