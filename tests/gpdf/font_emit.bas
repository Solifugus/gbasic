' The documents tests/run_gpdf_font.sh hands to outside readers. Every line
' drawn in an embedded font comes from tests/gpdf/font_lines.json, which the
' oracle (check_font.py) reads too: what was asked for, never what was written.
'   args[0] = output directory, args[1] = the fixture fonts' directory,
'   args[2] = a system TrueType font for dejavu.pdf, or "none"
program main(args)
    load gpdf
    out = args[0]
    fonts = args[1]
    lines {file}= "tests/gpdf/font_lines.json"
    spec = json_decode(read(lines))
    when = {datetime}"2026-11-10 09:00:00"

    ' fixture.pdf: the fixture's lines, a core-font line between them (two
    ' kinds of font on one page), and the font again on page two
    f = gpdf.font_file(join_path(fonts, "fixture.ttf"))
    d = gpdf.document({ title: "Łódź statement", created: when })
    d = gpdf.add_page(d)
    y = 700
    k = 0
    for each line in spec.fixture
        d = gpdf.set_font(d, f, line.size)
        d = gpdf.text_at(d, 72, y, line.text)
        y = y - 30
        if k = 0 then
            d = gpdf.set_font(d, "Helvetica", 12)
            d = gpdf.text_at(d, 72, y, "Helvetica between")
            y = y - 30
        end if
        k = k + 1
    end for
    d = gpdf.add_page(d)
    d = gpdf.set_font(d, f, 12)
    d = gpdf.text_at(d, 72, 700, "Łódź")
    gpdf.save(d, join_path(out, "fixture.pdf"))

    ' table.pdf: a table, a paragraph and page numbers in an embedded font, the
    ' three places a font is measured rather than only drawn
    d = gpdf.document({ title: "table", created: when })
    d = gpdf.add_page(d)
    d = gpdf.set_font(d, f, 10)
    d = gpdf.paragraph(d, 120, "Zakład Mechaniczny Łódź Zakład Mechaniczny Łódź")
    rows = [ { name: "Łódź", amount: 123 }, { name: "Zakład", amount: 321 } ]
    d = gpdf.table(d, rows, { columns: [ { name: "name", heading: "Zakład", width: 120 },
                                         { name: "amount", heading: "123", width: 60, align: "right" } ],
                              heading_font: f })
    d = gpdf.number_pages(d, { format: "Łódź {n}", font: f })
    gpdf.save(d, join_path(out, "table.pdf"))

    ' pair.pdf: the second font of a collection
    two = gpdf.font_file(join_path(fonts, "pair.ttc"), { index: 1 })
    d = gpdf.document({ title: "pair", created: when })
    d = gpdf.add_page(d)
    d = gpdf.set_font(d, two, 12)
    d = gpdf.text_at(d, 72, 700, "Łódź")
    gpdf.save(d, join_path(out, "pair.pdf"))

    if args[2] != "none" then
        dv = gpdf.font_file(args[2])
        d = gpdf.document({ title: "Łódź, İzmir, Новосибирск", created: when })
        d = gpdf.add_page(d)
        y = 700
        for each line in spec.dejavu
            d = gpdf.set_font(d, dv, line.size)
            d = gpdf.text_at(d, 72, y, line.text)
            y = y - 30
        end for
        gpdf.save(d, join_path(out, "dejavu.pdf"))
    end if
    return nothing
end program
