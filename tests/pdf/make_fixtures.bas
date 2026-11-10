' The PDFs tests/run_pdf.sh reads, made per run by gpdf, gBASIC's writer, so
' every answer the test expects is something it told the writer to put there.
' Pass 1 (args[0] = dir, args[1] = "text") writes the text pages; the runner
' then renders text.pdf to page.png with pdftoppm at 100 dpi; pass 2
' ("pictures") writes the pages that hold that picture.
program main(args)
    load gpdf
    dir = args[0]

    if args[1] = "text" then
        ' two lines, at known places: x 72, baselines 700 and 680 (from the bottom)
        d = gpdf.document({ title: "text" })
        d = gpdf.add_page(d)
        d = gpdf.set_font(d, "Helvetica", 12)
        d = gpdf.text_at(d, 72, 700, "Invoice No: FF-10442")
        d = gpdf.text_at(d, 72, 680, "Total 52.04")
        n = gpdf.save(d, join_path(dir, "text.pdf"))

        ' three pages: text, blank, text
        d = gpdf.document({ title: "three" })
        d = gpdf.add_page(d)
        d = gpdf.set_font(d, "Helvetica", 12)
        d = gpdf.text_at(d, 72, 700, "first")
        d = gpdf.add_page(d)
        d = gpdf.add_page(d)
        d = gpdf.text_at(d, 100, 500, "third")
        n = gpdf.save(d, join_path(dir, "three.pdf"))
        return nothing
    end if

    img = join_path(dir, "page.png")
    ' a scan: the picture of text.pdf, the size of the page
    d = gpdf.document({ title: "scan" })
    d = gpdf.add_page(d)
    d = gpdf.image(d, img, 0, 0, { width: 612, height: 792 })
    n = gpdf.save(d, join_path(dir, "scan.pdf"))

    ' a searchable scan: the picture, with a text layer over it
    d = gpdf.document({ title: "searchable" })
    d = gpdf.add_page(d)
    d = gpdf.image(d, img, 0, 0, { width: 612, height: 792 })
    d = gpdf.set_font(d, "Helvetica", 12)
    d = gpdf.text_at(d, 72, 700, "LAYER WORDS")
    n = gpdf.save(d, join_path(dir, "searchable.pdf"))

    ' text with a small picture: a logo at x 400, y 650 (its bottom), 61 x 79
    d = gpdf.document({ title: "logo" })
    d = gpdf.add_page(d)
    d = gpdf.set_font(d, "Helvetica", 12)
    d = gpdf.text_at(d, 72, 700, "Invoice No: FF-10443")
    d = gpdf.image(d, img, 400, 650, { width: 61, height: 79 })
    n = gpdf.save(d, join_path(dir, "logo.pdf"))

    ' two pictures the size of the page
    d = gpdf.document({ title: "two" })
    d = gpdf.add_page(d)
    d = gpdf.image(d, img, 0, 0, { width: 612, height: 792 })
    d = gpdf.image(d, img, 10, 10, { width: 600, height: 780 })
    n = gpdf.save(d, join_path(dir, "two.pdf"))

    ' small pictures and no text
    d = gpdf.document({ title: "small" })
    d = gpdf.add_page(d)
    d = gpdf.image(d, img, 100, 100, { width: 61, height: 79 })
    n = gpdf.save(d, join_path(dir, "small.pdf"))
    return nothing
end program
