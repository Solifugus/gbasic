' chart.bas Phase 4 (docs/chart_design.md): area, pie, heatmap, sparkline.
' The GOLDEN pins the rendering; the paper-computed ORACLE additions live in
' tests/chart_oracle_test.bas (pie cardinals need no trig; heatmap colors are
' hand-lerped hex; sparkline endpoints are two-point arithmetic).

program main(args)
    load chart from "../stdlib/chart.bas"

    ' Pie: quarter shares, a hostile label, a title.
    s = chart.spec("pie", { k: ["A&B", "c", "d", "e"], v: [1, 1, 1, 1] })
    s = chart.x(s, "k")
    s = chart.y(s, "v")
    s = chart.title(s, "shares")
    print(chart.render(s))

    ' Heatmap: exact endpoints, an exact midpoint, and an unknown (gray) cell.
    print(chart.heatmap(["x", "y"], [[0, 0.5], [unknown, 1]]))

    ' Sparkline: tiny, axis-less, gap in the middle.
    print(chart.sparkline([3, 1, 4, unknown, 5, 9]))

    ' Area: zero-anchored fill, broken by a gap.
    print(chart.area_xy([1, 2, 3, 4, 5], [2, 5, unknown, 3, 4]))
end program
