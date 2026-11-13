' The PDFs tests/run_pdf.sh reads, made per run by gpdf, gBASIC's writer, so
' every answer the test expects is something it told the writer to put there.
' Pass 1 (args[0] = dir, args[1] = "text") writes the text pages; the runner
' then renders text.pdf to page.png with pdftoppm at 100 dpi, and full.pdf at
' 300 dpi turned four ways; pass 2 ("pictures") writes the pages that hold
' those pictures.
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
        gpdf.save(d, join_path(dir, "text.pdf"))

        ' three pages: text, blank, text
        d = gpdf.document({ title: "three" })
        d = gpdf.add_page(d)
        d = gpdf.set_font(d, "Helvetica", 12)
        d = gpdf.text_at(d, 72, 700, "first")
        d = gpdf.add_page(d)
        d = gpdf.add_page(d)
        d = gpdf.text_at(d, 100, 500, "third")
        gpdf.save(d, join_path(dir, "three.pdf"))

        ' a fuller page, the same two lines and eight more, for the turned
        ' scans: tesseract's orientation check skips a page with too few
        ' characters (measured: the two-line page, at 100 and 300 dpi)
        d = gpdf.document({ title: "full" })
        d = gpdf.add_page(d)
        d = gpdf.set_font(d, "Helvetica", 12)
        d = gpdf.text_at(d, 72, 700, "Invoice No: FF-10442")
        d = gpdf.text_at(d, 72, 680, "Total 52.04")
        lines = ["Delivered to the warehouse on the morning of the fourth.",
                 "Twelve cartons of printer paper, white, letter size.",
                 "Four boxes of blue ballpoint pens, medium point.",
                 "Two staplers and one thousand staples to fit them.",
                 "Payment is due within thirty days of the invoice date.",
                 "Please quote the invoice number with every payment.",
                 "Questions about this invoice go to the accounts office.",
                 "Thank you for your business, and for paying on time."]
        y = 640
        for each l in lines
            d = gpdf.text_at(d, 72, y, l)
            y = y - 20
        end for
        gpdf.save(d, join_path(dir, "full.pdf"))
        return nothing
    end if

    img = join_path(dir, "page.png")
    ' a scan: the picture of text.pdf, the size of the page
    d = gpdf.document({ title: "scan" })
    d = gpdf.add_page(d)
    d = gpdf.image(d, img, 0, 0, { width: 612, height: 792 })
    gpdf.save(d, join_path(dir, "scan.pdf"))

    ' a searchable scan: the picture, with a text layer over it
    d = gpdf.document({ title: "searchable" })
    d = gpdf.add_page(d)
    d = gpdf.image(d, img, 0, 0, { width: 612, height: 792 })
    d = gpdf.set_font(d, "Helvetica", 12)
    d = gpdf.text_at(d, 72, 700, "LAYER WORDS")
    gpdf.save(d, join_path(dir, "searchable.pdf"))

    ' text with a small picture: a logo at x 400, y 650 (its bottom), 61 x 79
    d = gpdf.document({ title: "logo" })
    d = gpdf.add_page(d)
    d = gpdf.set_font(d, "Helvetica", 12)
    d = gpdf.text_at(d, 72, 700, "Invoice No: FF-10443")
    d = gpdf.image(d, img, 400, 650, { width: 61, height: 79 })
    gpdf.save(d, join_path(dir, "logo.pdf"))

    ' two pictures the size of the page
    d = gpdf.document({ title: "two" })
    d = gpdf.add_page(d)
    d = gpdf.image(d, img, 0, 0, { width: 612, height: 792 })
    d = gpdf.image(d, img, 10, 10, { width: 600, height: 780 })
    gpdf.save(d, join_path(dir, "two.pdf"))

    ' small pictures and no text
    d = gpdf.document({ title: "small" })
    d = gpdf.add_page(d)
    d = gpdf.image(d, img, 100, 100, { width: 61, height: 79 })
    gpdf.save(d, join_path(dir, "small.pdf"))

    ' TURNED SCANS (book gap G66): the fuller page at 300 dpi, turned 0, 90,
    ' 180 and 270 degrees counter-clockwise by the runner (PIL), each placed
    ' with a different margin on every side -- left 20, top 8, right 12,
    ' bottom 14 -- so a swapped axis or a margin taken from the wrong side
    ' cannot pass. A quarter turn is on a landscape page, as a sideways feed
    ' makes it. Absent when the runner had no PIL.
    for each a in [0, 90, 180, 270]
        img = join_path(dir, "turned" + string(a) + ".png")
        if exists(img) then
            w = 612
            h = 792
            if a = 90 or a = 270 then
                w = 792
                h = 612
            end if
            d = gpdf.document({ title: "turned " + string(a), size: { width: w, height: h } })
            d = gpdf.add_page(d)
            d = gpdf.image(d, img, 20, 14, { width: w - 32, height: h - 22 })
            gpdf.save(d, join_path(dir, "turned" + string(a) + ".pdf"))
        end if
    end for
    return nothing
end program
