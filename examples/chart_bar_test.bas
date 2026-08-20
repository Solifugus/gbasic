' chart.bas Phase 2 (docs/chart_design.md): bar + histogram. The GOLDEN pins
' the rendering; the band-math ORACLE lives in tests/chart_oracle_test.bas.
' Covered: grouped bars with a NEGATIVE value (drawn downward from the zero
' anchor) and a money value at face value; stacked bars; a histogram with
' fixed bins and one with the auto sqrt(n) rule; the bar_xy escape hatch.

program main(args)
    load chart from "../stdlib/chart.bas"

    p(USD)= 1.75
    df = {
        q: ["Q1", "Q2", "Q3", "Q4"],
        eps: [1.2, 1.5, -0.4, p],
        div: [0.5, 0.5, 0.5, 0.6]
    }
    print(chart.bar(df, "q", ["eps", "div"]))

    s = chart.spec("bar", df)
    s = chart.x(s, "q")
    s = chart.y(s, ["eps", "div"])
    s = chart.options(s, { stacked: true, y_min: 0 })
    ' the negative eps would be refused stacked; clamp it away for the golden
    df2 = {
        q: ["Q1", "Q2", "Q3", "Q4"],
        eps: [1.2, 1.5, 0.4, 1.75],
        div: [0.5, 0.5, 0.5, 0.6]
    }
    s = chart.options(chart.y(chart.x(chart.spec("bar", df2), "q"), ["eps", "div"]), { stacked: true })
    print(chart.render(s))

    print(chart.histogram_xy([1, 2, 2, 3, 3, 3, 4, 4, 5, 9]))

    h = chart.spec("histogram", { r: [0, 1, 2, 3, unknown, number("nan")] })
    h = chart.x(h, "r")
    h = chart.options(h, { bins: 2 })
    print(chart.render(h))

    print(chart.bar_xy(["a&b", "c"], [3, 4]))
end program
