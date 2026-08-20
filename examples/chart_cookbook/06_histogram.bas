' Recipe 6 — A histogram bins one value column; the count axis stays whole.
'
' Bin count is the bins: option or ceil(sqrt(n)) -- deterministic either
' way. unknown and nan observations are skipped. The count axis only ever
' labels whole numbers, because half an observation is not a thing.

program main(args)
    load chart from "../../stdlib/chart.bas"

    returns = [0.1, 0.3, 0.2, 0.25, -0.1, 0.15, 0.4, 0.18, 0.22, unknown]
    svg = chart.histogram_xy(returns)
    print "auto bins: " + string(len(svg)) + " chars"

    h = chart.spec("histogram", { r: returns })
    h = chart.x(h, "r")
    h = chart.options(h, { bins: 4 })
    print "fixed bins: " + string(len(chart.render(h))) + " chars"
end program
