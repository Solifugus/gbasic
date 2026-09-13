' Produce the documents the runner hands to mutool, ghostscript and poppler.
' Deliberately awkward shapes: accents from both WinAnsi ranges, a string full
' of the three bytes that end a PDF literal, multiple pages, both compressed
' and not.
program main(args)
    load gpdf
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
end program
