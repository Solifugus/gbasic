' Recipe 8 — A heatmap for the correlation-matrix shape.
'
' chart.heatmap(labels, matrix) colors a square matrix on a diverging
' blue-white-red scale. Fix the domain with heat_min/heat_max -- a
' correlation matrix wants -1..1 regardless of what this sample happened to
' produce, so equal colors mean equal correlations across every chart you
' make. An unknown cell renders GRAY with no number: missing shown as
' missing, never as a value.

program main(args)
    load chart from "../../stdlib/chart.bas"

    names = ["price", "volume", "spread"]
    m = [
        [1,       0.62,    -0.18],
        [0.62,    1,       unknown],
        [-0.18,   unknown, 1]
    ]
    h = chart.spec("heatmap", { rows: names, cols: names, matrix: m })
    h = chart.options(h, { heat_min: -1, heat_max: 1 })
    svg = chart.render(h)

    print "diagonal is the hot stop: " + string(contains(svg, "#b2182b"))
    print "missing cells are gray:   " + string(contains(svg, "#eeeeee"))
    print string(len(svg)) + " chars"
end program
