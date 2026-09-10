' Recipe 11 -- a categorical x, an axis that reads like the table beside it,
' and marks a browser can identify.
program main(args)
    load chart from "../../stdlib/chart.bas"

    ' "Revenue by month" is the commonest dashboard chart there is, and its x
    ' is almost always a period LABEL rather than a number.
    df = { month: ["2026-01", "2026-02", "2026-03", "2026-04"],
           revenue: [4200.0, 8100.0, 6400.0, 9300.0] }

    ' A line over a categorical x places its points at the same band centres a
    ' bar uses, so the two line up when a dashboard shows both.
    ln = chart.line(df, "month", ["revenue"])
    br = chart.bar(df, "month", ["revenue"])
    print "line points: " + string(count(match_all(ln, "L[0-9]")) + 1)
    print "bar rects:   " + string(count(match_all(br, "<rect")))
    print "month labels on the line: " + string(count(match_all(ln, ">2026-0")))

    ' The axis can be made to agree with the numbers printed next to it.
    money = chart.render(chart.options(
        chart.y(chart.x(chart.spec("line", df), "month"), ["revenue"]),
        { y_prefix: "$", y_decimals: 2, title: "Revenue" }))
    print "money axis: " + string(contains(money, ">$4,000.00<"))

    ' A fraction reads as a percentage without charting a different number:
    ' y_scale is display only and moves no geometry.
    rates = { month: ["2026-01", "2026-02"], margin: [0.42, 0.58] }
    pct = chart.render(chart.options(
        chart.y(chart.x(chart.spec("line", rates), "month"), ["margin"]),
        { y_scale: 100, y_suffix: "%", y_decimals: 0 }))
    print "percent axis: " + string(contains(pct, ">50%<"))

    ' Opt in and every DATA mark says which series and which point it is. A
    ' legend swatch is not a data mark and carries nothing, so DOM order never
    ' has to be trusted.
    keyed = chart.render(chart.options(
        chart.y(chart.x(chart.spec("bar", df), "month"), ["revenue"]),
        { mark_keys: true }))
    print "keyed marks: " + string(count(match_all(keyed, "data-key=")))
    print "first bar:   " + match_all(keyed, "data-series=\"[a-z]+\" data-key=\"[0-9-]+\"")[0].text
end program
