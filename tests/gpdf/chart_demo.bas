program main(args)
    load gpdf
    load chart
    when {datetime}= "2026-01-01 12:00:00"
    q = { quarter: [1, 2, 3, 4, 5, 6, 7, 8],
          revenue: [310, 345, unknown, 420, 462, 501, 548, 610],
          costs:   [270, 280, 285, 300, 315, 330, 355, 370] }
    s = chart.spec("line", q)
    s = chart.x(s, "quarter")
    s = chart.y(s, ["revenue", "costs"])
    s = chart.title(s, "Revenue and costs")
    s = chart.size(s, 460, 220)
    s = chart.options(s, { markers: true })

    d = gpdf.document({ created: when, title: "Report with a chart" })
    d = gpdf.add_page(d)
    d = gpdf.set_font(d, "Helvetica-Bold", 16)
    d = gpdf.text(d, "Quarterly report")
    d = gpdf.svg(d, chart.render(s), 72, 480, {})
    print string(gpdf.save(d, args[0] + "/chart.pdf"))
end program
