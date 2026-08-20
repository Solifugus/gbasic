' chart.bas Phase 1 (docs/chart_design.md): line + scatter as deterministic
' SVG text. The GOLDEN pins determinism and the full rendering; the numeric
' ORACLE lives in tests/run_chart.sh, which asserts hand-computed pixel
' coordinates under fixed margins. Covered here: multi-series with a legend,
' an unknown AND a nan breaking a line into gaps (never dropping to zero), a
' HOSTILE title that must arrive escaped, money values plotted at face value,
' a date x-axis labeled with real dates, the *_xy escape hatch, and the
' no-data rendering.

program main(args)
    load chart from "../stdlib/chart.bas"

    rev = {
        m: [1, 2, 3, 4, 5, 6],
        revenue: [1200, 1500, unknown, 2100, 2400, 2650],
        cost: [900, 950, 1000, 1100, number("nan"), 1300]
    }
    s = chart.spec("line", rev)
    s = chart.x(s, "m")
    s = chart.y(s, ["revenue", "cost"])
    s = chart.title(s, "R&D <units> " + chr(34) + "q" + chr(34))
    s = chart.size(s, 480, 280)
    print(chart.render(s))

    ' Dates on x: labels must come back as real dates, not day numbers.
    ' Money on y: plotted at face value, no conversion dance.
    d1 (date)= "2026-01-31"
    p1(USD)= 1200.50
    p2(USD)= 1350.25
    p3(USD)= 1180.75
    df2 = {
        day: [d1, d1 + 14 days, d1 + 28 days],
        balance: [p1, p2, p3]
    }
    print(chart.line(df2, "day", "balance"))

    ' The escape hatch, and scatter circles.
    print(chart.scatter_xy([1, 2, 3, 4], [4, 3, 5, 1]))

    ' No data: axes and an explicit note, never an error.
    print(chart.line_xy([], []))
end program
