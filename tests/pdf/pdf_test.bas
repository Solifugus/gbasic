' stdlib/pdf.bas against the PDFs tests/pdf/make_fixtures.bas wrote. Every
' expected answer is what the fixtures told gpdf to put there -- a word's text
' and left edge, its baseline (792 - y, as PDF counts y from the bottom), a
' picture's place and pixel size -- never poppler's own answer read back.
' args[0] = the fixtures' directory; args[1] = "ocr" adds the OCR checks, and
' "turned" the turned scans' (G66).
program main(args)
    load pdf
    load ocr
    dir = args[0]
    want_ocr = count(args) > 1 and args[1] = "ocr"
    want_turned = count(args) > 1 and args[1] = "turned"
    checks = { n: 0, bad: 0 }

    function check(checks, label, got, want)
        checks.n = checks.n + 1
        if type(got) = type(want) then
            if got = want then
                return checks
            end if
        end if
        checks.bad = checks.bad + 1
        print "MISMATCH " + label + ": got " + type(got) + " " + string(got) + ", want " + type(want) + " " + string(want)
        return checks
    end function

    function f(dir, name)
        return join_path(dir, name + ".pdf")
    end function

    function texts(words)
        out = []
        for each w in words
            append(out, w.text)
        end for
        return join(out, " ")
    end function

    ' does the baseline gpdf was given fall inside the word's box?
    function on_baseline(w, y)
        b = 792 - y
        return w.top < b and w.top + w.height > b
    end function

    ' OCR's box is the ink's, so a word with no descender ends at its baseline
    function ink_on_baseline(w, y)
        return abs(w.top + w.height - (792 - y)) < 2
    end function

    checks = check(checks, "available", pdf.available(), true)

    ' --- the document ---------------------------------------------------
    i = pdf.info(f(dir, "three"))
    checks = check(checks, "info ok", i.ok, true)
    checks = check(checks, "pages", i.pages, 3)
    checks = check(checks, "title", i.title, "three")
    checks = check(checks, "a size", i.sizes[2].width, 612)
    checks = check(checks, "its height", i.sizes[2].height, 792)
    checks = check(checks, "its rotation", i.sizes[2].rotation, 0)
    checks = check(checks, "not encrypted", i.encrypted, false)

    ' --- a text page ------------------------------------------------------
    p = pdf.read_page(f(dir, "text"), 1)
    checks = check(checks, "text: ok", p.ok, true)
    checks = check(checks, "text: how", p.how, "text")
    checks = check(checks, "text: words", texts(p.words), "Invoice No: FF-10442 Total 52.04")
    checks = check(checks, "text: left edge", p.words[0].left, 72)
    checks = check(checks, "text: first baseline", on_baseline(p.words[0], 700), true)
    checks = check(checks, "text: second baseline", on_baseline(p.words[3], 680), true)
    checks = check(checks, "text: not the other line's", on_baseline(p.words[3], 700), false)
    checks = check(checks, "text: confidence unmeasured", is_unknown(p.words[0].confidence), true)
    checks = check(checks, "text: one line", p.words[0].line = p.words[2].line, true)
    checks = check(checks, "text: no pictures", count(p.images), 0)
    checks = check(checks, "text: no layer", p.text_layer, false)
    checks = check(checks, "text: skew", is_unknown(p.skew), true)
    checks = check(checks, "text: page width", p.width, 612)
    checks = check(checks, "text: in points", p.units, "points")
    checks = check(checks, "text: no scan to have turned", is_unknown(p.scan_rotation), true)
    ' one grid serves both: a text page lays out as ocr lays out a scan
    g = split(ocr.grid(p), chr(10))
    checks = check(checks, "grid: two lines", count(g), 2)
    checks = check(checks, "grid: first", trim(g[0]), "Invoice No: FF-10442")
    checks = check(checks, "grid: second", trim(g[1]), "Total 52.04")
    t = pdf.text(f(dir, "text"), 1)
    checks = check(checks, "text laid out", contains(t, "Invoice No: FF-10442"), true)
    checks = check(checks, "text laid out, line two", contains(t, "Total 52.04"), true)
    checks = check(checks, "text has no form feed", contains(t, chr(12)), false)

    ' --- every page, in order ---------------------------------------------
    r = pdf.read(f(dir, "three"))
    checks = check(checks, "read: ok", r.ok, true)
    checks = check(checks, "read: three pages", count(r.pages), 3)
    hows = []
    for each pg in r.pages
        append(hows, pg.how)
    end for
    checks = check(checks, "read: how each", join(hows, ","), "text,blank,text")
    checks = check(checks, "read: page 3's word", r.pages[2].words[0].text, "third")
    checks = check(checks, "read: its left", r.pages[2].words[0].left, 100)
    checks = check(checks, "read: its baseline", on_baseline(r.pages[2].words[0], 500), true)
    checks = check(checks, "read: n", r.pages[2].n, 3)

    ' --- a scan, not read ---------------------------------------------------
    s = pdf.read_page(f(dir, "scan"), 1)
    checks = check(checks, "scan: how", s.how, "scan")
    checks = check(checks, "scan: no words", count(s.words), 0)
    checks = check(checks, "scan: one picture", count(s.images), 1)
    checks = check(checks, "scan: at 0,0", s.images[0].left + s.images[0].top, 0)
    checks = check(checks, "scan: page wide", s.images[0].width, 612)
    checks = check(checks, "scan: pixels wide (612 pt at 100 dpi)", s.images[0].pixels_wide, 850)
    checks = check(checks, "scan: pixels high", s.images[0].pixels_high, 1100)
    checks = check(checks, "scan: no layer", s.text_layer, false)

    ' --- a searchable scan: its layer's words, said to be a layer ------------
    l = pdf.read_page(f(dir, "searchable"), 1)
    checks = check(checks, "searchable: how", l.how, "scan")
    checks = check(checks, "searchable: layer", l.text_layer, true)
    checks = check(checks, "searchable: its words", texts(l.words), "LAYER WORDS")
    checks = check(checks, "searchable: not set aside", l.text_layer_set_aside, false)

    ' --- text with a logo: listed, never read -------------------------------
    o = pdf.read_page(f(dir, "logo"), 1, { ocr: true })
    checks = check(checks, "logo: how, though ocr was asked", o.how, "text")
    checks = check(checks, "logo: words", texts(o.words), "Invoice No: FF-10443")
    checks = check(checks, "logo: one picture", count(o.images), 1)
    checks = check(checks, "logo: left", o.images[0].left, 400)
    checks = check(checks, "logo: top (792 - 650 - 79)", o.images[0].top, 63)
    checks = check(checks, "logo: width", o.images[0].width, 61)
    checks = check(checks, "logo: height", o.images[0].height, 79)
    checks = check(checks, "logo: pixels", string(o.images[0].pixels_wide) + "x" + string(o.images[0].pixels_high), "850x1100")
    out = temp_dir()
    im = pdf.image(f(dir, "logo"), 1, 0, out)
    checks = check(checks, "image: written", exists(im), true)
    checks = check(checks, "image: as stored, a PNG", extension(im), "png")
    checks = check(checks, "image: its bytes are the picture's", byte_count(read({file}im)) > 1000, true)

    ' --- what is not decided is refused, as a value --------------------------
    w = pdf.read_page(f(dir, "two"), 1)
    checks = check(checks, "two: ok", w.ok, false)
    checks = check(checks, "two: why", w.why, "page 1 holds 2 pictures the size of the page, so which is the page is not decided")
    checks = check(checks, "two: its pictures", count(w.images), 2)
    m = pdf.read_page(f(dir, "small"), 1)
    checks = check(checks, "small: ok", m.ok, false)
    checks = check(checks, "small: why", contains(m.why, "only pictures smaller than the page"), true)

    ' --- a file cut short: poppler's own words, as a value -------------------
    c = pdf.info(f(dir, "cut"))
    checks = check(checks, "cut: ok", c.ok, false)
    checks = check(checks, "cut: why given", byte_count(c.why) > 0, true)
    c = pdf.read_page(f(dir, "cut"), 1)
    checks = check(checks, "cut: page ok", c.ok, false)
    c = pdf.read(f(dir, "cut"))
    checks = check(checks, "cut: read ok", c.ok, false)

    ' --- a wrong program raises ------------------------------------------------
    on error goto next
    x = pdf.read_page(f(dir, "text"), 2)
    checks = check(checks, "page 2 of 1", error.message, "pdf.read_page: there is no page 2 (the document has 1)")
    error.clear()
    x = pdf.read_page(f(dir, "text"), 0)
    checks = check(checks, "page 0", error.message, "pdf.read_page: there is no page 0 (the document has 1)")
    error.clear()
    x = pdf.read_page(f(dir, "text"), 1, { orc: true })
    checks = check(checks, "a misspelt option", error.message, "pdf.read_page: unknown option 'orc' (known: ocr, languages)")
    error.clear()
    x = pdf.read_page(f(dir, "text"), 1, { ocr: "yes" })
    checks = check(checks, "ocr not a boolean", error.message, "pdf.read_page: ocr is true or false")
    error.clear()
    x = pdf.text(f(dir, "cut"), 1)
    checks = check(checks, "text of a cut file raises", contains(error.message, "pdf.text: "), true)
    error.clear()
    x = pdf.image(f(dir, "logo"), 1, 1, out)
    checks = check(checks, "no second picture", error.message, "pdf.image: page 1 has 1 pictures, so there is no picture 1")
    error.clear()
    on error stop

    ' --- OCR, only when asked --------------------------------------------------
    if want_ocr then
        s = pdf.read_page(f(dir, "scan"), 1, { ocr: true })
        checks = check(checks, "ocr: how", s.how, "ocr")
        checks = check(checks, "ocr: words", texts(s.words), "Invoice No: FF-10442 Total 52.04")
        checks = check(checks, "ocr: confidence measured", type(s.words[0].confidence), "number")
        ' in the page's points, not the picture's pixels
        checks = check(checks, "ocr: left near 72 pt", abs(s.words[0].left - 72) < 3, true)
        checks = check(checks, "ocr: first baseline", ink_on_baseline(s.words[0], 700), true)
        checks = check(checks, "ocr: second baseline", ink_on_baseline(s.words[3], 680), true)
        checks = check(checks, "ocr: not the other line's", ink_on_baseline(s.words[3], 700), false)
        checks = check(checks, "ocr: skew measured", type(s.skew), "number")
        checks = check(checks, "ocr: no layer", s.text_layer_set_aside, false)
        g = split(ocr.grid(s), chr(10))
        checks = check(checks, "ocr grid: first", trim(g[0]), "Invoice No: FF-10442")
        l = pdf.read_page(f(dir, "searchable"), 1, { ocr: true })
        checks = check(checks, "ocr searchable: how", l.how, "ocr")
        checks = check(checks, "ocr searchable: layer set aside", l.text_layer_set_aside, true)
        checks = check(checks, "ocr searchable: the picture's words", contains(texts(l.words), "FF-10442"), true)
        checks = check(checks, "ocr searchable: not the layer's", contains(texts(l.words), "LAYER"), false)
    end if

    ' --- turned scans (book gap G66) ---------------------------------------
    ' The fixture placed each picture with margins left 20, top 8, right 12,
    ' bottom 14. Read upright, the page is 612 x 792 again, and the picture's
    ' margins are where turning the page puts them, worked out by hand:
    '   0    as placed                       left 20, top 8   580 x 770
    '   90   turned clockwise to read: the bottom edge comes to the left and
    '        the left edge to the top        left 14, top 20  590 x 760
    '   180  right to left, bottom to top    left 12, top 14  580 x 770
    '   270  turned anticlockwise: the top edge to the left, the right edge
    '        to the top                      left 8, top 12   590 x 760
    ' A word gpdf put at x 72 on baseline 700 of the 612 x 792 page is then at
    ' left + 72 * (picture width / 612), its ink ending at top + 92 * (picture
    ' height / 792).
    if want_turned then
        turns = [{ a: 0, left: 20, top: 8, w: 580, h: 770 },
                 { a: 90, left: 14, top: 20, w: 590, h: 760 },
                 { a: 180, left: 12, top: 14, w: 580, h: 770 },
                 { a: 270, left: 8, top: 12, w: 590, h: 760 }]
        for each t in turns
            k = "turned " + string(t.a) + ": "
            s = pdf.read_page(f(dir, "turned" + string(t.a)), 1, { ocr: true })
            checks = check(checks, k + "how", s.how, "ocr")
            checks = check(checks, k + "the turn ocr found", s.scan_rotation, t.a)
            checks = check(checks, k + "with its confidence", type(s.scan_rotation_confidence), "number")
            checks = check(checks, k + "width as read", s.width, 612)
            checks = check(checks, k + "height as read", s.height, 792)
            checks = check(checks, k + "in points", s.units, "points")
            im = s.images[0]
            checks = check(checks, k + "the picture, turned with the page",
                           string(im.left) + "," + string(im.top) + " " + string(im.width) + "x" + string(im.height),
                           string(t.left) + "," + string(t.top) + " " + string(t.w) + "x" + string(t.h))
            sx = t.w / 612
            sy = t.h / 792
            inv = unknown
            tot = unknown
            for each w in s.words
                if w.text = "Invoice" and is_unknown(inv) then
                    inv = w
                end if
                if w.text = "Total" and is_unknown(tot) then
                    tot = w
                end if
            end for
            checks = check(checks, k + "Invoice read", is_unknown(inv), false)
            checks = check(checks, k + "Total read", is_unknown(tot), false)
            if not is_unknown(inv) and not is_unknown(tot) then
                checks = check(checks, k + "Invoice's left edge", abs(inv.left - (t.left + 72 * sx)) < 3, true)
                checks = check(checks, k + "on its baseline", abs(inv.top + inv.height - (t.top + 92 * sy)) < 2, true)
                checks = check(checks, k + "Total on the next", abs(tot.top + tot.height - (t.top + 112 * sy)) < 2, true)
            end if
            g = split(ocr.grid(s), chr(10))
            checks = check(checks, k + "grid's first line", trim(g[0]), "Invoice No: FF-10442")
        end for
    end if

    remove_tree(out)
    print "checks: " + string(checks.n)
    print "mismatches: " + string(checks.bad)
end program
