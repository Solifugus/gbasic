' Recipe 7 — A pie shows shares of a whole, which is exactly why it refuses.
'
' A NEGATIVE share is nonsense, and an UNKNOWN share silently misstates
' every other share -- so unlike a line (where a gap is honest absence),
' a pie refuses both, by name. Slices run clockwise from 12 o'clock in row
' order, and the legend always carries the percentages, computed for you.

program main(args)
    load chart from "../../stdlib/chart.bas"

    spend = { dept: ["Eng", "Sales", "Ops"], budget: [6, 3, 1] }
    svg = chart.pie(spend, "dept", "budget")

    print "legend shares: " + string(contains(svg, "Eng 60%")) + " " + string(contains(svg, "Ops 10%"))
    print string(len(svg)) + " chars"
end program
