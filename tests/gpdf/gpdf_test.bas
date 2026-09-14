' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' `gpdf` phase 1 -- the value model, measurement, wrapping and the refusals.
'
' SELF-CHECKING RATHER THAN GOLDEN. A PDF is bytes; a golden of one records
' whatever came out and defends it, and every defect here is a PLAUSIBLE
' DOCUMENT -- a line measured against the wrong widths still lays out, a
' substituted character still prints, an xref off by forty bytes still opens in
' a forgiving viewer. The structural half of this is checked by three
' INDEPENDENT READERS in the runner, which is the only oracle that is not us.

function check(label, got, want)
    G.checks = G.checks + 1
    if string(got) = string(want) then
        print "ok   " + label
    else
        G.mismatches = G.mismatches + 1
        print "MISMATCH " + label + ": got " + string(got) + ", want " + string(want)
    end if
    return nothing
end function

program main(args)
    load gpdf
    G = { checks: 0, mismatches: 0 }

    print "-- measurement is against the published core-14 metrics"
    ' Computed by hand from the Adobe widths, not read back from the library:
    ' H 722 + e 556 + l 222 + l 222 + o 556 = 2278/1000 * 12 = 27.336
    check("Helvetica 'Hello' at 12pt", gpdf.text_width("Helvetica", 12, "Hello"), 27.336)
    check("Courier is monospaced (5 * 600/1000 * 12)", gpdf.text_width("Courier", 12, "Hello"), 36)
    check("and scales with size", gpdf.text_width("Helvetica", 24, "Hello"), 54.672)
    check("the empty string is zero wide", gpdf.text_width("Helvetica", 12, ""), 0)
    ' THE FONTS ARE DIFFERENT, which is what a shared or defaulted width table
    ' would hide -- every check above passes on a library that returns
    ' Helvetica's widths for everything.
    check("Times differs from Helvetica on the same text",
          gpdf.text_width("Times-Roman", 12, "Hello") = gpdf.text_width("Helvetica", 12, "Hello"), false)
    check("and bold is wider than regular",
          gpdf.text_width("Helvetica-Bold", 12, "Hello") > gpdf.text_width("Helvetica", 12, "Hello"), true)

    print ""
    print "-- wrapping fits the column, and that is a MEASUREMENT not a count"
    ' A character count would break these two identically; the widths do not.
    narrow = gpdf.wrap("Helvetica", 12, 100, "iiiii iiiii iiiii iiiii")
    wide   = gpdf.wrap("Helvetica", 12, 100, "MMMMM MMMMM MMMMM MMMMM")
    check("same character count, different line count", count(narrow) = count(wide), false)
    check("every wrapped line fits the width",
          _all_fit("Helvetica", 12, 100, gpdf.wrap("Helvetica", 12, 100,
                   "This paragraph is long enough to need several lines at this width.")), true)
    check("an explicit newline always breaks",
          count(gpdf.wrap("Helvetica", 12, 400, "one" + chr(10) + "two")), 2)
    ' A word that cannot fit is BROKEN, not allowed to overflow the column --
    ' running silently into the next column is the worse answer.
    long = gpdf.wrap("Helvetica", 10, 40, "ABCDEFGHIJKLMNOPQRSTUVWXYZ")
    check("an over-long word is broken rather than overflowing", count(long) > 1, true)
    check("and every piece of it fits", _all_fit("Helvetica", 10, 40, long), true)
    check("a width narrower than one character still terminates",
          count(gpdf.wrap("Helvetica", 12, 1, "AB")) >= 2, true)

    print ""
    print "-- encoding: refused by name, never substituted"
    check("Latin-1 is representable", gpdf.representable("Müller"), true)
    check("so are the Windows additions", gpdf.representable("€ — ’ ½"), true)
    check("Cyrillic is not", gpdf.representable("Владимир"), false)
    check("nor is CJK", gpdf.representable("日本語"), false)
    on error goto next
    gpdf.text_width("Helvetica", 12, "Владимир")
    check("and measuring it refuses", contains(error.message, "cannot be written in WinAnsi"), true)
    check("naming the character", contains(error.message, "U+0412"), true)
    check("and where it is", contains(error.message, "character 1 of 8"), true)
    error.clear()
    on error stop
    ' THE CONTROL. Without it "unrepresentable is refused" is satisfied by a
    ' library that refuses everything.
    check("CONTROL: representable text measures fine",
          gpdf.text_width("Helvetica", 12, "Müller") > 0, true)

    print ""
    print "-- the document"
    when {datetime}= "2026-01-01 00:00:00"
    d = gpdf.document({ created: when })
    check("a fresh document has no pages", gpdf.page_count(d), 0)
    d = gpdf.add_page(d)
    check("add_page returns a document with one", gpdf.page_count(d), 1)
    d = gpdf.set_font(d, "Helvetica", 12)
    d = gpdf.text(d, "Hello")
    d = gpdf.add_page(d)
    check("and two", gpdf.page_count(d), 2)
    ' A gBASIC record is a VALUE, so a function cannot write back through its
    ' argument. Asserted because an API that appeared to mutate would silently
    ' do nothing to the caller's document.
    before = gpdf.document({ created: when })
    after = gpdf.add_page(before)
    check("the original is untouched", gpdf.page_count(before), 0)
    check("and the returned one is not", gpdf.page_count(after), 1)

    print ""
    print "-- a paragraph that runs past the margin adds its own pages"
    ' Asserted as a DIFFERENCE against a control: "it added a page" alone is
    ' satisfied by a library that adds one per line, and "it did not" by one
    ' that silently writes past the bottom of the sheet.
    p1 = gpdf.set_font(gpdf.add_page(gpdf.document({})), "Helvetica", 11)
    short = gpdf.paragraph(p1, 450, "One short line.")
    check("a short paragraph stays on one page", gpdf.page_count(short), 1)
    long_body = ""
    li = 0
    while li < 200
        long_body = long_body + "Line " + string(li) + " of a statement that continues for some while. "
        li = li + 1
    end while
    spilled = gpdf.paragraph(p1, 450, long_body)
    check("a long one spills onto more", gpdf.page_count(spilled) > 1, true)
    ' And it must not run off the bottom: every page break happens above the
    ' margin, so the last cursor is inside the page.
    check("and never writes below the margin", spilled.cursor.y >= spilled.margin - spilled.font_size * 1.2, true)

    print ""
    print "-- rendering is deterministic and structurally sound"
    a = gpdf.render(d)
    b = gpdf.render(d)
    check("two renders are byte-identical", a = b, true)
    check("it begins with a PDF header", left(a, 8), "%PDF-1.4")
    check("and ends at %%EOF", contains(a, "%%EOF"), true)
    ' THE OFFSET CHECK, and it is the one this library was written to get
    ' right: `startxref` must be the byte offset where `xref` actually begins.
    check("startxref points at the real xref", _startxref_correct(a), true)
    ' A document with no `created` carries NO date rather than a clock reading,
    ' which is what keeps the bytes stable.
    undated = gpdf.render(gpdf.add_page(gpdf.document({})))
    check("an undated document carries no CreationDate", contains(undated, "CreationDate"), false)
    check("a dated one does", contains(a, "CreationDate"), true)

    print ""
    print "-- tables: the thing every business document is"
    money_rows = []
    ti = 1
    while ti <= 5
        amt {USD}= 10.00 * ti
        append(money_rows, { item: "Item " + string(ti), amount: amt })
        ti = ti + 1
    end while
    tspec = { columns: [ { name: "item", heading: "Item", width: 200 },
                         { name: "amount", heading: "Amount", width: 80, align: "right", total: true } ] }
    t = gpdf.set_font(gpdf.add_page(gpdf.document({})), "Helvetica", 10)
    t2 = gpdf.table(t, money_rows, tspec)
    check("a short table stays on one page", gpdf.page_count(t2), 1)
    check("and the cursor has moved down the page", t2.cursor.y < t.cursor.y, true)

    ' A FRAME is accepted as well as rows, because that is what the
    ' spreadsheet, accounting and statistics layers produce.
    fr = { item: [ "a", "b" ], amount: [ 1, 2 ] }
    fspec = { columns: [ { name: "item", heading: "Item", width: 100 },
                         { name: "amount", heading: "N", width: 60, align: "right" } ] }
    check("a frame is accepted like rows",
          gpdf.page_count(gpdf.table(t, fr, fspec)), 1)

    ' TOTALS ADD WITH THE VALUE'S OWN ARITHMETIC, so two currencies are refused
    ' by `money` rather than silently producing a number that means nothing.
    ' The table never learns what a currency is.
    usd {USD}= 100.00
    eur {EUR}= 50.00
    on error goto next
    gpdf.table(t, [ { item: "a", amount: usd }, { item: "b", amount: eur } ], tspec)
    check("a total across two currencies is refused", contains(error.message, "different currencies"), true)
    error.clear()
    on error stop
    check("CONTROL: one currency totals fine", gpdf.page_count(t2), 1)

    print ""
    print "-- the totals row carries its label ONCE"
    ' "Have we passed a total column yet" writes the label into every column
    ' before the first total, so a three-column table reads `Total   Total`.
    ' Counted rather than merely looked for, because `contains(page, "Total")`
    ' is true either way.
    three = { columns: [ { name: "a", heading: "A", width: 90 },
                         { name: "b", heading: "B", width: 90 },
                         { name: "amount", heading: "Amount", width: 80, align: "right", total: true } ] }
    tot = gpdf.table(gpdf.set_font(gpdf.add_page(gpdf.document({ compress: false })), "Helvetica", 10),
                     [ { a: "x", b: "y", amount: 1 }, { a: "p", b: "q", amount: 2 } ], three)
    check("the label is drawn exactly once", _count_drawn(tot, "Total"), 1)
    check("and the total is the sum", _count_drawn(tot, "3"), 1)

    print ""
    print "-- a tall row moves WHOLE, and the heading follows it"
    ' Splitting a row would put a description on one page and its amount on the
    ' next, which reads as two different transactions. Asserted as a DIFFERENCE:
    ' the same row count with TALL rows must take more pages than with short
    ' ones, which is only true if a row is an indivisible unit.
    short_rows = []
    tall_rows = []
    ri = 1
    while ri <= 40
        append(short_rows, { item: "Item " + string(ri), amount: ri })
        append(tall_rows, { item: "Item " + string(ri) + ": a description long enough to wrap onto several lines inside a narrow column, making this row much taller than one line", amount: ri })
        ri = ri + 1
    end while
    narrow = { columns: [ { name: "item", heading: "Item", width: 200 },
                          { name: "amount", heading: "N", width: 60, align: "right" } ] }
    p_short = gpdf.page_count(gpdf.table(t, short_rows, narrow))
    p_tall  = gpdf.page_count(gpdf.table(t, tall_rows, narrow))
    check("tall rows need more pages than short ones", p_tall > p_short, true)
    check("and short ones still fit on few", p_short <= 2, true)

    print ""
    print "-- nothing a table draws falls below the bottom margin"
    ' A page break measured on ONE LINE rather than the row's real height does
    ' not split the row -- the row stays whole and runs off the bottom of the
    ' page, into the footer. Nothing errors, the file is structurally perfect,
    ' and the page number has a sentence written through it. So the assertion
    ' is about DRAWN POSITIONS: every text placement a table makes must sit at
    ' or above the margin.
    overflow = gpdf.table(t, tall_rows, narrow)
    check("every drawn line is above the bottom margin",
          _lowest_text(overflow) >= overflow.margin, true)
    ' The CONTROL, without which the check is satisfied by a table that draws
    ' nothing at all.
    check("CONTROL: and the table did draw something",
          _lowest_text(overflow) < overflow.size.height, true)

    print ""
    print "-- page numbers, which need a total nobody knows until the end"
    many = gpdf.table(t, tall_rows, narrow)
    numbered = gpdf.number_pages(many, {})
    check("numbering does not change the page count", gpdf.page_count(numbered), gpdf.page_count(many))
    check("and every page grew some content",
          _all_pages_nonempty(numbered), true)
    ' Asserted on the PAGE CONTENT, not the rendered bytes: with compression on
    ' (the default) the text is inside a deflate stream and `contains` on the
    ' file would be false for a working library.
    check("the format is a caller's to choose",
          contains(gpdf.number_pages(t2, { format: "Sheet {n}/{total}" }).pages[0].content, "Sheet 1/1"), true)

    print ""
    print "-- charts become PDF VECTORS, and the arcs land on the circle"
    ' PDF has no arc operator, so a pie's arcs become cubic Beziers. THE
    ' PICTURE IS NOT THE TEST: with the centre chosen on the wrong side every
    ' slice bowed INWARD into a four-pointed star, and the file was
    ' structurally perfect, ghostscript was happy and the text extracted fine.
    ' Only geometry catches it -- so the curve is SAMPLED and every point must
    ' lie on the circle it claims to be part of.
    arc_svg = ("<svg width=\"200\" height=\"200\">" +
               "<path d=\"M150 100 A50 50 0 0 1 100 150 Z\" fill=\"#000000\"/></svg>")
    av = gpdf.svg(gpdf.set_font(gpdf.add_page(gpdf.document({ compress: false })), "Helvetica", 10),
                  arc_svg, 0, 0, {})
    ' The SVG circle is centred at (100,100) r=50; placed at y=0 with height
    ' 200, PDF y = 200 - svg y, so the centre is (100, 100) again.
    check("every sampled point of the arc is on the circle",
          _arc_on_circle(av.pages[0].content, 100, 100, 50), true)
    ' THE CONTROL: the same check against a deliberately wrong radius must
    ' FAIL, or "points are on the circle" is satisfied by a test that always
    ' says yes.
    check("CONTROL: and it does not say yes to the wrong circle",
          _arc_on_circle(av.pages[0].content, 100, 100, 70), false)

    ' A chart's text survives as TEXT, which is the whole point of vectors
    ' over a bitmap -- it is searchable, selectable and scales.
    line_svg = "<svg width=\"100\" height=\"50\" font-size=\"12\"><text x=\"10\" y=\"20\">Revenue</text></svg>"
    lv = gpdf.svg(gpdf.set_font(gpdf.add_page(gpdf.document({ compress: false })), "Helvetica", 10),
                  line_svg, 0, 0, {})
    check("a chart label is drawn as text", _count_drawn(lv, "Revenue"), 1)

    ' AND AN ELEMENT OUTSIDE THE SUBSET IS REFUSED BY NAME rather than
    ' dropped, so the day `chart` grows one we are told instead of losing part
    ' of the picture.
    on error goto next
    gpdf.svg(lv, "<svg width=\"10\" height=\"10\"><ellipse cx=\"1\" cy=\"1\"/></svg>", 0, 0, {})
    check("an unknown element is refused by name", contains(error.message, "<ellipse>"), true)
    error.clear()
    gpdf.svg(lv, "<svg width=\"10\" height=\"10\"><path d=\"M1 1 C2 2 3 3 4 4\"/></svg>", 0, 0, {})
    check("an unknown path command is refused by name", contains(error.message, "'C'"), true)
    error.clear()
    gpdf.svg(lv, "<p>not svg</p>", 0, 0, {})
    check("something that is not svg at all is refused", contains(error.message, "does not start with an <svg>"), true)
    error.clear()
    on error stop
    check("CONTROL: the subset itself still translates", _count_drawn(lv, "Revenue"), 1)

    print ""
    print "-- images are COPIED, not re-encoded"
    img = gpdf.set_font(gpdf.add_page(gpdf.document({})), "Helvetica", 10)
    rgb = gpdf.image(img, "tests/gpdf/images/rgb.png", 50, 50, {})
    check("a PNG embeds", count(rgb.images), 1)
    check("with its own pixel dimensions", rgb.images[0].width, 60)
    check("and its own colour space", rgb.images[0].space, "/DeviceRGB")
    check("grey PNGs too", gpdf.image(img, "tests/gpdf/images/gray.png", 0, 0, {}).images[0].space, "/DeviceGray")
    ' A palette PNG carries its PLTE into an /Indexed space -- that is what
    ' makes the pass-through possible for the commonest kind of logo.
    pal = gpdf.image(img, "tests/gpdf/images/palette.png", 0, 0, {})
    check("a palette PNG becomes /Indexed", contains(pal.images[0].space, "/Indexed"), true)
    jpg = gpdf.image(img, "tests/gpdf/images/photo.jpg", 0, 0, {})
    check("a JPEG embeds as DCT", jpg.images[0].filter, "/DCTDecode")
    check("with its size read from the frame header", jpg.images[0].width, 80)

    ' THE CLAIM IS THAT NOTHING IS DECODED. A JPEG's stream is the FILE, byte
    ' for byte -- so the embedded data must be exactly as long as the file on
    ' disk. Re-encoding would still produce a valid PDF showing the right
    ' picture, at a different size and quality, and nothing would say so.
    jf {file}= "tests/gpdf/images/photo.jpg"
    check("the JPEG stream is the file itself", byte_count(jpg.images[0].data), bytes(jf))

    ' AND EVERY IDAT CHUNK, not just the first. A PNG of any size has several
    ' -- the mascot has six -- and taking one gives a truncated zlib stream
    ' that still produces a structurally perfect PDF: the dictionary declares
    ' the right dimensions, mupdf lists the image at the right size, and the
    ' picture is simply cut off. So the assertion is that the embedded stream
    ' is nearly the whole FILE, which is false the moment a chunk is dropped
    ' (the mascot's first IDAT is under a fifth of it).
    big = gpdf.image(img, "docs/assets/mascot.png", 0, 0, {})
    mf {file}= "docs/assets/mascot.png"
    check("every IDAT chunk is concatenated, not just the first",
          byte_count(big.images[0].data) > bytes(mf) * 0.9, true)
    ' The CONTROL: it is also not MORE than the file, which would mean chunk
    ' headers or trailing bytes had been swept in with the pixel data.
    check("and no more than the file itself", byte_count(big.images[0].data) < bytes(mf), true)

    ' The same file twice is stored ONCE -- a logo on forty pages is one
    ' stream, not forty.
    twice = gpdf.image(gpdf.image(img, "tests/gpdf/images/rgb.png", 0, 0, {}),
                       "tests/gpdf/images/rgb.png", 0, 100, {})
    check("the same image twice is stored once", count(twice.images), 1)
    check("and drawn twice", _count_op(twice, "/Im1 Do"), 2)

    ' Aspect ratio is kept when only one dimension is given, because a squashed
    ' logo is the commonest way to get this wrong.
    wide = gpdf.image(img, "tests/gpdf/images/rgb.png", 0, 0, { width: 120 })
    check("giving only a width keeps the aspect ratio", contains(wide.pages[0].content, "120 0 0 80"), true)
    check("and natural size is the pixel count", contains(rgb.pages[0].content, "60 0 0 40"), true)

    on error goto next
    gpdf.image(img, "tests/gpdf/images/interlaced.png", 0, 0, {})
    check("an interlaced PNG is refused by name", contains(error.message, "INTERLACED"), true)
    error.clear()
    gpdf.image(img, "tests/gpdf/images/alpha.png", 0, 0, {})
    check("an alpha channel is refused rather than dropped", contains(error.message, "ALPHA CHANNEL"), true)
    error.clear()
    gpdf.image(img, "tests/gpdf/images/nope.png", 0, 0, {})
    check("a missing file is refused", contains(error.message, "there is no file"), true)
    error.clear()
    gpdf.image(img, "stdlib/gpdf.bas", 0, 0, {})
    check("something that is not an image is refused", contains(error.message, "neither a PNG nor a JPEG"), true)
    error.clear()
    on error stop
    check("CONTROL: a supported image still embeds", count(rgb.images), 1)

    print ""
    print "-- refusals, each beside its nearest legal neighbour"
    on error goto next
    gpdf.set_font(d, "Comic Sans", 12)
    check("a non-core font is refused", contains(error.message, "not a core-14 font"), true)
    error.clear()
    gpdf.set_font(d, "Helvetica", 0)
    check("a zero font size is refused", contains(error.message, "positive number of points"), true)
    error.clear()
    gpdf.document({ size: "tabloid" })
    check("an unknown page size is refused", contains(error.message, "unknown size"), true)
    error.clear()
    gpdf.document({ tilte: "typo" })
    check("an unknown option is refused BY NAME", contains(error.message, "unknown option 'tilte'"), true)
    error.clear()
    gpdf.render(gpdf.document({}))
    check("rendering no pages is refused", contains(error.message, "no pages"), true)
    error.clear()
    gpdf.text(gpdf.document({}), "x")
    check("writing before add_page is refused", contains(error.message, "no page yet"), true)
    error.clear()
    gpdf.wrap("Helvetica", 12, 0, "x")
    check("a zero wrap width is refused", contains(error.message, "positive number of points"), true)
    error.clear()
    gpdf.table(d, [], { columns: [] })
    check("a table with no columns is refused", contains(error.message, "`columns` array"), true)
    error.clear()
    gpdf.table(d, [], { columns: [ { heading: "x", width: 10 } ] })
    check("a column with no name is refused", contains(error.message, "has no `name`"), true)
    error.clear()
    gpdf.table(d, [], { columns: [ { name: "x" } ] })
    check("a column with no width is refused", contains(error.message, "positive `width`"), true)
    error.clear()
    gpdf.table(d, [], { colunms: [] })
    check("a misspelled spec key is refused BY NAME", contains(error.message, "unknown option 'colunms'"), true)
    error.clear()
    on error stop
    check("CONTROL: a well-formed document still renders", byte_count(a) > 400, true)

    print ""
    print "checks: " + string(G.checks)
    print "mismatches: " + string(G.mismatches)
end program

' Walk the `c` operators of a content stream and sample each cubic. Every
' point on an arc must sit on the circle the arc claims -- within a tenth of a
' point, which is far tighter than a Bezier's own approximation error over 90
' degrees and far looser than the arithmetic's noise.
function _arc_on_circle(content, cx, cy, r)
    cur = [ 0, 0 ]
    seen = 0
    for each line in split(content, chr(10))
        parts = split(trim(line), " ")
        n = count(parts)
        if n >= 3 and parts[n - 1] = "m" then
            cur = [ number(parts[n - 3]), number(parts[n - 2]) ]
        end if
        if n >= 7 and parts[n - 1] = "c" then
            p1 = [ number(parts[n - 7]), number(parts[n - 6]) ]
            p2 = [ number(parts[n - 5]), number(parts[n - 4]) ]
            p3 = [ number(parts[n - 3]), number(parts[n - 2]) ]
            ' A cubic starting ON the circle: sample it and measure.
            if _near_circle(cur, cx, cy, r) then
                for each t in [ 0.25, 0.5, 0.75 ]
                    pt = _bezier(cur, p1, p2, p3, t)
                    if not _near_circle(pt, cx, cy, r) then return false
                    seen = seen + 1
                end for
            end if
            cur = p3
        end if
    end for
    ' The check must have had something to check.
    return seen >= 3
end function

function _bezier(p0, p1, p2, p3, t)
    u = 1 - t
    a = u * u * u
    b = 3 * u * u * t
    c = 3 * u * t * t
    d = t * t * t
    return [ a * p0[0] + b * p1[0] + c * p2[0] + d * p3[0],
             a * p0[1] + b * p1[1] + c * p2[1] + d * p3[1] ]
end function

function _near_circle(p, cx, cy, r)
    dx = p[0] - cx
    dy = p[1] - cy
    return abs(sqrt(dx * dx + dy * dy) - r) < 0.1
end function

' How many times an operator appears in the content.
function _count_op(doc, op)
    n = 0
    for each p in doc.pages
        n = n + count(split(p.content, op)) - 1
    end for
    return n
end function

' How many times a string was DRAWN -- content shows text as `(...) Tj`, so
' counting the parenthesised occurrences counts placements rather than
' substring hits anywhere in the stream.
function _count_drawn(doc, needle)
    n = 0
    for each p in doc.pages
        for each piece in split(p.content, "(" + needle + ") Tj")
            n = n + 1
        end for
    end for
    return n - count(doc.pages)
end function

' The lowest y any text was placed at, across every page. Content is built as
' `... 1 0 0 1 X Y Tm (text) Tj ...`, so the y is the token before `Tm`.
function _lowest_text(doc)
    low = doc.size.height
    for each p in doc.pages
        for each piece in split(p.content, " Tm ")
            parts = split(piece, " ")
            if count(parts) >= 1 then
                y = number_or(parts[count(parts) - 1])
                if not is_unknown(y) and y < low then low = y
            end if
        end for
    end for
    return low
end function

function number_or(t)
    on error goto next
    n = number(t)
    if error then
        error.clear()
        return unknown
    end if
    on error stop
    return n
end function

function _all_pages_nonempty(doc)
    for each p in doc.pages
        if len(p.content) = 0 then return false
    end for
    return true
end function

function _all_fit(font, size, width, lines)
    for each l in lines
        if gpdf.text_width(font, size, l) > width then return false
    end for
    return true
end function

' `startxref N` must name the byte at which the `xref` keyword begins.
function _startxref_correct(pdf)
    marker = chr(10) + "startxref" + chr(10)
    at = find(pdf, marker)
    if is_unknown(at) then return false
    rest = mid(pdf, at + len(marker), 20)
    digits = ""
    i = 0
    while i < len(rest)
        c = mid(rest, i, 1)
        if c >= "0" and c <= "9" then
            digits = digits + c
        else
            i = len(rest)
        end if
        i = i + 1
    end while
    if digits = "" then return false
    said = number(digits)
    real = find(pdf, chr(10) + "xref" + chr(10))
    if is_unknown(real) then return false
    ' `find` is in codepoints; the offset in the file is in BYTES.
    return said = byte_count(left(pdf, real + 1))
end function
