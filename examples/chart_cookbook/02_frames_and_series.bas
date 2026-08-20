' Recipe 2 — Frames first: name your columns, get multiple series.
'
' The primary calls take the same frame shape frame.bas produces: a record of
' equal-length column lists. A LIST of y-column names makes one series each,
' with its own Okabe-Ito palette color and a legend entry. An `unknown` cell
' BREAKS the line -- a gap where the data is missing, never a drop to zero
' that would invent a number. nan behaves the same way.

program main(args)
    load chart from "../../stdlib/chart.bas"

    quarters = {
        q: [1, 2, 3, 4, 5, 6],
        revenue: [1200, 1500, unknown, 2100, 2400, 2650],
        cost: [900, 950, 1000, 1100, 1150, 1300]
    }
    svg = chart.line(quarters, "q", ["revenue", "cost"])

    ' the gap is real: the revenue path RESTARTS with a mid-path M command,
    ' which only a break produces
    print "two series:  " + string(contains(svg, ">cost</text>"))
    print "gap in path: " + string(contains(svg, " M"))
    print string(len(svg)) + " chars"
end program
