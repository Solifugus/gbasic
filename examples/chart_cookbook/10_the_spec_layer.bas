' Recipe 10 — The spec layer: a chart is a record you build up and inspect.
'
' The one-liners are wrappers over spec + render. Build the spec in steps
' when you want titles, fixed bounds (so two dossiers compare on the same
' scale), your own palette, or the chart.page() HTML wrapper for
' save-and-open. The constructor is chart.spec -- `new` is a reserved word
' a library function cannot be named.

program main(args)
    load chart from "../../stdlib/chart.bas"

    df = { m: [1, 2, 3, 4], actual: [10, 14, 9, 16], plan: [12, 12, 12, 12] }
    s = chart.spec("line", df)
    s = chart.x(s, "m")
    s = chart.y(s, ["actual", "plan"])
    s = chart.title(s, "Actual vs plan")
    s = chart.size(s, 480, 240)
    s = chart.options(s, { y_min: 0, y_max: 20, markers: true })

    print "spec is data: kind=" + s.kind + " x=" + s.x + " series=" + string(count(s.y))
    svg = chart.render(s)
    print "title present: " + string(contains(svg, ">Actual vs plan</text>"))

    html = chart.page(s)
    print left(html, 15) + " ... " + string(len(html)) + " chars of standalone page"
end program
