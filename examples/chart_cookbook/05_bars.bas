' Recipe 5 — Bars anchor at zero; grouped and stacked are one option apart.
'
' A bar's length is its statement, so the y-axis ALWAYS includes zero --
' clipping the baseline lies. Grouped bars may go negative (drawn downward);
' stacked bars with negatives are refused outright, because a naive stack
' overlaps into nonsense that looks fine at a glance. A duplicate category
' is refused by name: aggregation belongs to the frame tooling, and a chart
' inventing a sum is a wrong picture.

program main(args)
    load chart from "../../stdlib/chart.bas"

    eps = {
        q: ["Q1", "Q2", "Q3", "Q4"],
        earned: [1.2, 1.5, -0.4, 1.75],
        paid: [0.5, 0.5, 0.5, 0.6]
    }
    grouped = chart.bar(eps, "q", ["earned", "paid"])
    print "grouped: " + string(len(grouped)) + " chars"

    up = {
        q: ["Q1", "Q2", "Q3", "Q4"],
        earned: [1.2, 1.5, 0.4, 1.75],
        paid: [0.5, 0.5, 0.5, 0.6]
    }
    s = chart.spec("bar", up)
    s = chart.x(s, "q")
    s = chart.y(s, ["earned", "paid"])
    s = chart.options(s, { stacked: true })
    stacked = chart.render(s)
    print "stacked: " + string(len(stacked)) + " chars"
end program
