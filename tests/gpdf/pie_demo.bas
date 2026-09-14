program main(args)
    load gpdf
    load chart
    when {datetime}= "2026-01-01 12:00:00"
    d = gpdf.document({ created: when })
    d = gpdf.add_page(d)

    ' Four equal quarters: the arc endpoints land on exact cardinal points,
    ' which is the case chart's own oracle tier uses because it needs no trig.
    s = chart.spec("pie", { label: ["a","b","c","d"], share: [0.25,0.25,0.25,0.25] })
    s = chart.x(s, "label")
    s = chart.y(s, ["share"])
    s = chart.size(s, 240, 240)
    d = gpdf.svg(d, chart.render(s), 60, 500, {})

    ' And an uneven one, where the arcs are not 90 degrees and one exceeds 180.
    s2 = chart.spec("pie", { label: ["big","small","tiny"], share: [0.7,0.2,0.1] })
    s2 = chart.x(s2, "label")
    s2 = chart.y(s2, ["share"])
    s2 = chart.size(s2, 240, 240)
    d = gpdf.svg(d, chart.render(s2), 320, 500, {})
    print string(gpdf.save(d, args[0] + "/pies.pdf"))
end program
