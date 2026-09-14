' Produce the documents the runner hands to mutool, ghostscript and poppler.
' Deliberately awkward shapes: accents from both WinAnsi ranges, a string full
' of the three bytes that end a PDF literal, multiple pages, both compressed
' and not.
program main(args)
    load gpdf
    load chart
    dir = args[0]
    when {datetime}= "2026-01-01 12:00:00"

    d = gpdf.document({ size: "letter", title: "gPDF acceptance",
                        author: "gBASIC", created: when })
    d = gpdf.add_page(d)
    d = gpdf.set_font(d, "Helvetica-Bold", 18)
    d = gpdf.text(d, "Invoice 1041")
    d = gpdf.set_font(d, "Helvetica", 11)
    d = gpdf.paragraph(d, 400, "This paragraph is wrapped by measuring every candidate line against the core-14 metrics, so it fits its column rather than approximately fitting it.")
    d = gpdf.add_page(d)
    d = gpdf.set_font(d, "Times-Roman", 12)
    d = gpdf.text(d, "Page two, in Times.")
    print string(gpdf.save(d, dir + "/basic.pdf"))

    ' Accents from BOTH ranges: Latin-1 (0xA0-0xFF) and the Windows additions
    ' (0x80-0x9F), which live elsewhere in Unicode and are the half a naive
    ' "just take the low byte" conversion gets wrong.
    a = gpdf.document({ created: when, compress: false })
    a = gpdf.add_page(a)
    a = gpdf.set_font(a, "Helvetica", 14)
    a = gpdf.text(a, "Müller Provençal naïve")
    a = gpdf.text(a, "€9.99 — ‘quoted’ ½ ±")
    print string(gpdf.save(a, dir + "/accents.pdf"))

    ' ( ) and backslash end or escape a PDF string literal. Unescaped, the
    ' document is corrupt from that byte on -- and it is ordinary invoice text.
    e = gpdf.document({ created: when })
    e = gpdf.add_page(e)
    e = gpdf.set_font(e, "Courier", 11)
    e = gpdf.text(e, "Ref (A) \\ (B) — 50% off (while stocks last)")
    print string(gpdf.save(e, dir + "/escapes.pdf"))

    ' Enough text to run past the bottom margin, so `paragraph` must add pages.
    m = gpdf.document({ created: when })
    m = gpdf.add_page(m)
    m = gpdf.set_font(m, "Helvetica", 11)
    body = ""
    i = 0
    while i < 200
        body = body + "Line " + string(i) + " of a long statement that continues for some while. "
        i = i + 1
    end while
    m = gpdf.paragraph(m, 450, body)
    print string(gpdf.page_count(m))
    print string(gpdf.save(m, dir + "/multipage.pdf"))

    ' A table that spans pages, with a row every seventh line tall enough to
    ' wrap. The runner asks poppler whether the heading repeats on every page
    ' and whether a tall row kept its lines together.
    t = gpdf.document({ created: when })
    t = gpdf.add_page(t)
    t = gpdf.set_font(t, "Helvetica", 10)
    rows = []
    k = 1
    while k <= 45
        amt {USD}= 99.99 + k
        desc = "Item " + string(k)
        if mod(k, 7) = 0 then
            desc = desc + ": a much longer description that has to wrap onto more than one line inside its own column, which makes this row taller than its neighbours"
        end if
        append(rows, { line: string(k), description: desc, amount: amt })
        k = k + 1
    end while
    t = gpdf.table(t, rows, { columns: [ { name: "line", heading: "#", width: 30, align: "right" },
                                         { name: "description", heading: "Description", width: 300 },
                                         { name: "amount", heading: "Amount", width: 90, align: "right", total: true } ] })
    t = gpdf.number_pages(t, {})
    print string(gpdf.page_count(t))
    print string(gpdf.save(t, dir + "/table.pdf"))

    ' A chart, as VECTORS. The readers check it parses; the text tier checks
    ' its labels come back as TEXT, which is the difference between a chart and
    ' a picture of one.
    cs = chart.spec("line", { quarter: [1,2,3,4,5,6,7,8],
                              revenue: [310,345,unknown,420,462,501,548,610],
                              costs:   [270,280,285,300,315,330,355,370] })
    cs = chart.x(cs, "quarter")
    cs = chart.y(cs, ["revenue", "costs"])
    cs = chart.title(cs, "Revenue and costs")
    cs = chart.size(cs, 460, 220)
    cs = chart.options(cs, { markers: true })
    c = gpdf.document({ created: when, title: "Report with a chart" })
    c = gpdf.add_page(c)
    c = gpdf.set_font(c, "Helvetica-Bold", 16)
    c = gpdf.text(c, "Quarterly report")
    c = gpdf.svg(c, chart.render(cs), 72, 480, {})
    ' And a pie, which is the arc path.
    ps = chart.spec("pie", { label: ["big","small","tiny"], share: [0.7,0.2,0.1] })
    ps = chart.x(ps, "label")
    ps = chart.y(ps, ["share"])
    ps = chart.size(ps, 240, 240)
    c = gpdf.svg(c, chart.render(ps), 72, 200, {})
    print string(gpdf.save(c, dir + "/chart.pdf"))

    ' A letterhead: a real palette PNG with six IDAT chunks, scaled by width.
    ' The readers check it parses AND that the image survived as an image.
    l = gpdf.document({ created: when, title: "Letterhead" })
    l = gpdf.add_page(l)
    l = gpdf.image(l, "docs/assets/mascot.png", 72, 620, { width: 300 })
    l = gpdf.set_font(l, "Helvetica-Bold", 16)
    l = gpdf.text_at(l, 72, 590, "gBASIC Consulting")
    l = gpdf.image(l, "tests/gpdf/images/photo.jpg", 72, 480, { width: 160 })
    print string(gpdf.save(l, dir + "/logo.pdf"))
end program
