' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' gpdf -- PDF documents, written from the specification.
'
' PHASE 1: the document, the core-14 fonts, measured text and word wrap.
' Written clean from ISO 32000 rather than ported from anything, so the
' licence is ours to set and the defects are ours to find.
'
' THREE DECISIONS SHAPE EVERYTHING ELSE.
'
' 1. THE OUTPUT IS DETERMINISTIC. Every other format this project writes is
'    byte-comparable -- charts deliberately, and the xlsx writer fixes ZIP
'    mod-times with a comment saying a clock there "would look like a
'    successful write while making byte comparison useless". A PDF carries a
'    CreationDate and an /ID, and taking either from the clock would make a
'    golden impossible. Both are pinnable, and `created:` takes a datetime.
'
' 2. AN UNREPRESENTABLE CHARACTER IS REFUSED BY NAME. Phase 1 is WinAnsi, so
'    it cannot write Cyrillic or CJK. The alternative to refusing is
'    substituting -- a question mark, a bullet, the low byte -- which puts a
'    wrong customer name on an invoice that has already been posted. This
'    project has met that failure in other people's I/O (FreeTDS silently
'    swallowing Japanese text) and will not ship it. Embedded fonts are a
'    later phase; when they land the refusal narrows, and nothing a caller
'    wrote has to change.
'
' 3. OFFSETS ARE COUNTED IN BYTES, NEVER CODEPOINTS. The cross-reference table
'    stores the byte offset of every object, and `len` counts codepoints, so
'    `byte_count` is used throughout. This is not hypothetical: the library
'    that inspired the phase-1 shape emits `startxref 753` for an xref that
'    is actually at byte 799, and every strict reader has to repair it.

library gpdf

    load gpdf_metrics

    ' ---- the document ----------------------------------------------------

    ' A page size in points (1/72 inch). A4 and Letter cover nearly everything;
    ' any { width:, height: } is accepted.
    function sizes()
        return { a4: { width: 595.28, height: 841.89 },
                 letter: { width: 612, height: 792 },
                 legal: { width: 612, height: 1008 },
                 a3: { width: 841.89, height: 1190.55 },
                 a5: { width: 420.94, height: 595.28 } }
    end function

    ' gpdf.document(options) -> a document value.
    '
    ' `created` is a datetime and is REQUIRED to be given for a byte-stable
    ' document; omitted, it is `unknown` and the file carries no CreationDate
    ' at all rather than a clock reading. A missing date is honest; an
    ' invented one is not, and a golden cannot tell the difference.
    function document(options)
        opts = _options(options, [ "size", "margin", "title", "author",
                                   "subject", "creator", "created", "compress" ],
                        "gpdf.document")
        size = _default(opts, "size", sizes().letter)
        if is_string(size) then
            known = sizes()
            if not has(known, lower(size)) then
                error "gpdf.document: unknown size '" + string(size) + "' (known: " + join(keys(known), ", ") + ")"
            end if
            size = known[lower(size)]
        end if
        if not is_record(size) or not has(size, "width") or not has(size, "height") then
            error "gpdf.document: size must be a name or a record of { width, height } in points"
        end if
        m = _default(opts, "margin", 72)
        return { size: size,
                 margin: m,
                 title: _default(opts, "title", ""),
                 author: _default(opts, "author", ""),
                 subject: _default(opts, "subject", ""),
                 creator: _default(opts, "creator", "gBASIC gpdf"),
                 created: _default(opts, "created", unknown),
                 compress: _default(opts, "compress", true),
                 pages: [],
                 font: "Helvetica",
                 font_size: 12,
                 fonts_used: [] }
    end function

    ' A page is a content stream plus the state a caller cares about. Pages are
    ' VALUES in an array, like every other collection here, so `add_page`
    ' returns the new document rather than mutating one -- a function cannot
    ' write back through its argument, and an API that appeared to would
    ' silently do nothing.
    function add_page(doc)
        out = doc
        append(out.pages, { content: "", size: doc.size })
        out.cursor = { x: doc.margin, y: doc.size.height - doc.margin }
        return out
    end function

    function page_count(doc)
        return count(doc.pages)
    end function

    ' ---- fonts and measurement -------------------------------------------

    function set_font(doc, name, size)
        w = gpdf_metrics.widths(name)
        if is_unknown(w) then
            error ("gpdf.set_font: '" + string(name) + "' is not a core-14 font (" +
                   join(gpdf_metrics.fonts(), ", ") + "). Embedded fonts are a later phase.")
        end if
        if not is_number(size) or size <= 0 then
            error "gpdf.set_font: size must be a positive number of points"
        end if
        out = doc
        out.font = name
        out.font_size = size
        if not contains(out.fonts_used, name) then
            append(out.fonts_used, name)
        end if
        return out
    end function

    ' The width of `text` in points at the given font and size. This is the
    ' function wrapping, centring and right-alignment are all built on, and the
    ' one that decides whether a column overflows -- so it measures the SAME
    ' bytes that will be written, after the WinAnsi conversion, rather than the
    ' caller's UTF-8.
    function text_width(font, size, text)
        w = gpdf_metrics.widths(font)
        if is_unknown(w) then
            error "gpdf.text_width: '" + string(font) + "' is not a core-14 font"
        end if
        enc = to_winansi(text, "gpdf.text_width")
        total = 0
        i = 0
        while i < byte_count(enc)
            b = byte_at(enc, i)
            total = total + w[b]
            i = i + 1
        end while
        return total * size / 1000
    end function

    ' ---- encoding --------------------------------------------------------

    ' UTF-8 in, WinAnsi out, or a refusal naming the character and where it is.
    ' A caller that wants to know BEFORE writing asks `representable`.
    function to_winansi(text, whose)
        s = string(text)
        out = ""
        i = 0
        n = len(s)
        while i < n
            ch = mid(s, i, 1)
            b = _winansi_byte(ch)
            if b < 0 then
                error (whose + ": '" + ch + "' (U+" + _u(code(ch)) +
                       ") cannot be written in WinAnsi, which is the only encoding this " +
                       "phase supports. It is character " + string(i + 1) + " of " +
                       string(n) + ". Embedded fonts are a later phase; nothing is " +
                       "substituted, because a wrong character on a posted document is " +
                       "worse than a refusal.")
            end if
            out = out + from_bytes([b])
            i = i + 1
        end while
        return out
    end function

    ' A codepoint in the U+XXXX form people actually look up. `hex_encode` of
    ' the character gives its UTF-8 BYTES, which look like a codepoint and are
    ' not one -- U+D092 for a letter that is really U+0412.
    function _u(n)
        h = upper(hex_encode(from_bytes([ floor(n / 256), n - 256 * floor(n / 256) ])))
        while len(h) > 4 and left(h, 1) = "0"
            h = mid(h, 1, len(h) - 1)
        end while
        while len(h) < 4
            h = "0" + h
        end while
        return h
    end function

    ' Can every character be written? Answers a BOOLEAN rather than raising, so
    ' an application can check a customer's name at the point it is entered
    ' instead of at the point a document is produced.
    function representable(text)
        s = string(text)
        i = 0
        while i < len(s)
            if _winansi_byte(mid(s, i, 1)) < 0 then
                return false
            end if
            i = i + 1
        end while
        return true
    end function

    ' The WinAnsi byte for one character, or -1. ASCII is itself; the rest is a
    ' lookup built from the same table the metrics were generated against.
    function _winansi_byte(ch)
        if byte_count(ch) = 1 then
            b = byte_at(ch, 0)
            if b >= 32 and b <= 126 then
                return b
            end if
            ' A lone high byte is already WinAnsi -- it cannot be UTF-8.
            if b >= 160 then
                return b
            end if
            return -1
        end if
        m = _high_map()
        if has(m, ch) then
            return m[ch]
        end if
        return -1
    end function

    ' The characters WinAnsi can write that are not Latin-1, plus the Latin-1
    ' range reached from UTF-8. Keyed by the character itself, so a caller's
    ' ordinary gBASIC string looks up directly.
    function _high_map()
        m = {}
        ' 0xA0-0xFF: Latin-1, where WinAnsi and Unicode agree on the codepoint.
        i = 160
        while i <= 255
            m[from_bytes([0xC0 + floor(i / 64), 0x80 + (i - 64 * floor(i / 64))])] = i
            i = i + 1
        end while
        ' 0x80-0x9F: the Windows additions, which are elsewhere in Unicode.
        m["€"] = 128
        m["‚"] = 130
        m["ƒ"] = 131
        m["„"] = 132
        m["…"] = 133
        m["†"] = 134
        m["‡"] = 135
        m["ˆ"] = 136
        m["‰"] = 137
        m["Š"] = 138
        m["‹"] = 139
        m["Œ"] = 140
        m["Ž"] = 142
        m["‘"] = 145
        m["’"] = 146
        m["“"] = 147
        m["”"] = 148
        m["•"] = 149
        m["–"] = 150
        m["—"] = 151
        m["˜"] = 152
        m["™"] = 153
        m["š"] = 154
        m["›"] = 155
        m["œ"] = 156
        m["ž"] = 158
        m["Ÿ"] = 159
        return m
    end function

    ' ---- drawing ---------------------------------------------------------

    ' Write one line at the current cursor and move down. `leading` defaults to
    ' 1.2x the font size, which is the convention every word processor uses.
    function text(doc, s)
        ' CHECKED HERE, not only in text_at: reading doc.cursor first would
        ' fail with "unknown record field: cursor", which is true, internal,
        ' and says nothing about what the caller did wrong. Reporting the wrong
        ' cause over the right one is a defect this project has shipped before.
        if count(doc.pages) = 0 then
            error "gpdf.text: there is no page yet -- call gpdf.add_page first"
        end if
        return text_at(doc, doc.cursor.x, doc.cursor.y, s)
    end function

    function text_at(doc, x, y, s)
        out = _draw(doc, x, y, s)
        out.cursor = { x: doc.cursor.x, y: y - out.font_size * 1.2 }
        return out
    end function

    ' Put text on the page and move NOTHING. A table positions every cell
    ' itself and must not have the cursor dragged along behind it.
    function _draw(doc, x, y, s)
        if count(doc.pages) = 0 then
            error "gpdf.text: there is no page yet -- call gpdf.add_page first"
        end if
        enc = to_winansi(s, "gpdf.text")
        out = doc
        i = count(out.pages) - 1
        body = ("BT /F" + string(_font_index(out, out.font)) + " " + _num(out.font_size) +
                " Tf 1 0 0 1 " + _num(x) + " " + _num(y) + " Tm (" + _escape(enc) + ") Tj ET" + chr(10))
        out.pages[i].content = out.pages[i].content + body
        return out
    end function

    ' A horizontal rule. Tables use it for the line under a heading and above a
    ' total; it is public because a caller drawing its own layout wants it too.
    function rule(doc, x1, y, x2, thickness)
        if count(doc.pages) = 0 then
            error "gpdf.rule: there is no page yet -- call gpdf.add_page first"
        end if
        out = doc
        i = count(out.pages) - 1
        out.pages[i].content = (out.pages[i].content + _num(thickness) + " w " +
                                _num(x1) + " " + _num(y) + " m " + _num(x2) + " " + _num(y) +
                                " l S" + chr(10))
        return out
    end function

    ' Break `s` into lines that fit `width` points. THE MEASUREMENT IS THE
    ' POINT: wrapping on character counts is what produces a ragged column that
    ' overflows on the one row with a wide word in it.
    '
    ' A word longer than the whole width is BROKEN rather than allowed to
    ' overflow -- an unbreakable 40-character part number in a 30mm column has
    ' to go somewhere, and running into the next column silently is the worse
    ' answer. An explicit newline in the input always breaks.
    function wrap(font, size, width, s)
        if not is_number(width) or width <= 0 then
            error "gpdf.wrap: width must be a positive number of points"
        end if
        lines = []
        for each para in split(string(s), chr(10))
            if trim(para) = "" then
                append(lines, "")
            else
                cur = ""
                for each word in split(para, " ")
                    if word != "" then
                        try = word
                        if cur != "" then try = cur + " " + word
                        if text_width(font, size, try) <= width then
                            cur = try
                        else
                            if cur != "" then
                                append(lines, cur)
                                cur = ""
                            end if
                            ' The word alone may still not fit.
                            while text_width(font, size, word) > width and len(word) > 1
                                take = _fit_prefix(font, size, width, word)
                                append(lines, left(word, take))
                                word = mid(word, take, len(word) - take)
                            end while
                            cur = word
                        end if
                    end if
                end for
                if cur != "" then append(lines, cur)
            end if
        end for
        return lines
    end function

    ' How many characters of `word` fit in `width`. At least one, always, or a
    ' width narrower than a single character would loop forever.
    function _fit_prefix(font, size, width, word)
        n = 1
        while n < len(word)
            if text_width(font, size, left(word, n + 1)) > width then
                return n
            end if
            n = n + 1
        end while
        return n
    end function

    ' Write wrapped text, adding pages as it runs out of room. Returns the
    ' document -- the page breaks are the library's business, not the caller's.
    function paragraph(doc, width, s)
        out = doc
        leading = out.font_size * 1.2
        for each line in wrap(out.font, out.font_size, width, s)
            if out.cursor.y < out.margin then
                out = add_page(out)
            end if
            if line = "" then
                out.cursor = { x: out.cursor.x, y: out.cursor.y - leading }
            else
                out = text(out, line)
            end if
        end for
        return out
    end function

    ' ---- tables ----------------------------------------------------------
    '
    ' The thing every business document is: a table that does not fit on one
    ' page. Invoices, statements, ledgers and reports are all this problem
    ' wearing different headings, and in a cursor API you write the page-break
    ' arithmetic by hand every time -- which is where the header stops
    ' repeating and a row lands half on each page.
    '
    ' A ROW IS NEVER SPLIT. A cell that wraps to three lines makes its row
    ' three lines tall, and the whole row moves to the next page if it does not
    ' fit. Splitting would put the first line of a description on one page and
    ' its amount on the next, which reads as two different transactions.

    ' gpdf.table(doc, rows, spec) -> the document, with the table drawn.
    '
    ' `rows` is an array of records, or a `frame` (a record of column ->
    ' array), so the spreadsheet, accounting and statistics layers feed it
    ' directly. A spec column is { name, heading, width, align, format, total }.
    function table(doc, rows, spec)
        data = _as_rows(rows)
        sp = _options(spec, [ "columns", "heading_font", "heading_size", "padding",
                              "rules", "totals_label", "x" ], "gpdf.table")
        cols = _default(sp, "columns", [])
        if not is_array(cols) or count(cols) = 0 then
            error "gpdf.table: the spec needs a `columns` array of { name, width }"
        end if
        i = 0
        while i < count(cols)
            c = _options(cols[i], [ "name", "heading", "width", "align", "format", "total" ],
                         "gpdf.table column " + string(i + 1))
            if not has(c, "name") then
                error "gpdf.table: column " + string(i + 1) + " has no `name` -- it must name a field of the data"
            end if
            if not has(c, "width") or not is_number(c.width) or c.width <= 0 then
                error "gpdf.table: column '" + string(c.name) + "' needs a positive `width` in points"
            end if
            i = i + 1
        end while

        head_font = _default(sp, "heading_font", _bold_of(doc.font))
        head_size = _default(sp, "heading_size", doc.font_size)
        pad = _default(sp, "padding", 4)
        rules = _default(sp, "rules", true)
        x0 = _default(sp, "x", doc.margin)

        body_font = doc.font
        body_size = doc.font_size
        leading = body_size * 1.2

        out = doc
        if count(out.pages) = 0 then
            out = add_page(out)
        end if
        out = _table_header(out, cols, x0, head_font, head_size, pad, rules)

        totals = {}
        for each c in cols
            if _default(c, "total", false) then
                totals[c.name] = unknown
            end if
        end for

        for each row in data
            cells = []
            height = leading
            for each c in cols
                txt = _cell_text(row, c)
                lines = wrap(body_font, body_size, c.width - 2 * pad, txt)
                append(cells, lines)
                if count(lines) * leading > height then
                    height = count(lines) * leading
                end if
            end for
            ' The row moves WHOLE, and the heading follows it.
            if out.cursor.y - height < out.margin then
                out = add_page(out)
                out = set_font(out, body_font, body_size)
                out = _table_header(out, cols, x0, head_font, head_size, pad, rules)
            end if
            out = set_font(out, body_font, body_size)
            out = _table_row(out, cols, cells, x0, pad, leading)
            out.cursor = { x: out.cursor.x, y: out.cursor.y - height }
            for each c in cols
                if has(totals, c.name) then
                    totals[c.name] = _add_total(totals[c.name], row[c.name], c.name)
                end if
            end for
        end for

        if count(keys(totals)) > 0 then
            if out.cursor.y - leading * 2 < out.margin then
                out = add_page(out)
                out = _table_header(out, cols, x0, head_font, head_size, pad, rules)
            end if
            if rules then
                out = rule(out, x0, out.cursor.y + leading * 0.25, x0 + _total_width(cols), 0.5)
            end if
            out = set_font(out, head_font, head_size)
            label = _default(sp, "totals_label", "Total")
            ' The label goes in the FIRST column and nowhere else. Putting it
            ' in every column before the first total -- which is what "have we
            ' seen a total yet" does -- writes `Total` two or three times
            ' across the row.
            cells = []
            ci = 0
            for each c in cols
                if has(totals, c.name) then
                    append(cells, [ _format_value(totals[c.name], _default(c, "format", "")) ])
                else
                    if ci = 0 then
                        append(cells, [ label ])
                    else
                        append(cells, [ "" ])
                    end if
                end if
                ci = ci + 1
            end for
            out = _table_row(out, cols, cells, x0, pad, leading)
            out.cursor = { x: out.cursor.x, y: out.cursor.y - leading }
            out = set_font(out, body_font, body_size)
        end if
        return out
    end function

    function _table_header(doc, cols, x0, font, size, pad, rules)
        out = set_font(doc, font, size)
        cells = []
        for each c in cols
            append(cells, [ string(_default(c, "heading", c.name)) ])
        end for
        out = _table_row(out, cols, cells, x0, pad, size * 1.2)
        out.cursor = { x: out.cursor.x, y: out.cursor.y - size * 1.2 }
        if rules then
            out = rule(out, x0, out.cursor.y + size * 0.3, x0 + _total_width(cols), 0.5)
        end if
        out.cursor = { x: out.cursor.x, y: out.cursor.y - size * 0.4 }
        return out
    end function

    ' One row of already-wrapped cells, each aligned in its own column.
    function _table_row(doc, cols, cells, x0, pad, leading)
        out = doc
        x = x0
        i = 0
        while i < count(cols)
            c = cols[i]
            lines = cells[i]
            align = _default(c, "align", "left")
            j = 0
            while j < count(lines)
                line = lines[j]
                w = text_width(out.font, out.font_size, line)
                tx = x + pad
                if align = "right" then
                    tx = x + c.width - pad - w
                end if
                if align = "center" then
                    tx = x + (c.width - w) / 2
                end if
                out = _draw(out, tx, out.cursor.y - j * leading, line)
                j = j + 1
            end while
            x = x + c.width
            i = i + 1
        end while
        return out
    end function

    function _total_width(cols)
        w = 0
        for each c in cols
            w = w + c.width
        end for
        return w
    end function

    function _bold_of(font)
        if font = "Helvetica" then return "Helvetica-Bold"
        if font = "Times-Roman" then return "Times-Bold"
        if font = "Courier" then return "Courier-Bold"
        return font
    end function

    ' Rows in, rows out -- accepting a `frame` (column -> array) as well, since
    ' that is what the spreadsheet and statistics layers produce.
    function _as_rows(rows)
        if is_array(rows) then
            return rows
        end if
        if not is_record(rows) then
            error "gpdf.table: the data must be an array of records or a frame"
        end if
        names = keys(rows)
        if count(names) = 0 then
            return []
        end if
        n = count(rows[names[0]])
        out = []
        i = 0
        while i < n
            r = {}
            for each nm in names
                r[nm] = rows[nm][i]
            end for
            append(out, r)
            i = i + 1
        end while
        return out
    end function

    ' A cell as text. `money` renders with its own currency and decimals and a
    ' date in its own form -- the whole reason those are value kinds rather
    ' than floats and strings somebody remembered to format.
    function _cell_text(row, c)
        if not has(row, c.name) then
            return ""
        end if
        return _format_value(row[c.name], _default(c, "format", ""))
    end function

    function _format_value(v, fmt)
        if is_unknown(v) then
            return ""
        end if
        if fmt != "" and is_number(v) then
            return _fixed(v, number(fmt))
        end if
        return string(v)
    end function

    function _fixed(n, places)
        p = 1
        i = 0
        while i < places
            p = p * 10
            i = i + 1
        end while
        r = floor(n * p + 0.5) / p
        s = string(r)
        if places > 0 then
            if not contains(s, ".") then s = s + "."
            after = len(s) - find(s, ".") - 1
            while after < places
                s = s + "0"
                after = after + 1
            end while
        end if
        return s
    end function

    ' Totals add with the value's OWN arithmetic, so money refuses to add two
    ' currencies rather than producing a number that means nothing.
    function _add_total(running, v, name)
        if is_unknown(v) then
            return running
        end if
        if is_unknown(running) then
            return v
        end if
        return running + v
    end function

    ' ---- charts, as vectors ----------------------------------------------
    '
    ' `chart` already renders to SVG, so a report wants that chart IN the PDF.
    ' This is not an SVG renderer and does not pretend to be one: it translates
    ' the SUBSET `chart` emits -- line, rect, circle, path, text -- into PDF
    ' vector operators, and REFUSES anything else BY NAME. We own both ends, so
    ' the day `chart` grows an element this says so instead of quietly dropping
    ' it, which is the only reason a subset translator is honest.
    '
    ' The result is real vectors: it scales, prints and selects as text, where
    ' a rasterised chart is a picture of a chart.
    '
    ' TWO COORDINATE SYSTEMS. SVG's y grows DOWNWARD from the top-left, PDF's
    ' grows UPWARD from the bottom-left. The flip is arithmetic per coordinate
    ' rather than a negative-y transform, because a negative-y CTM would mirror
    ' the TEXT as well and the labels would come out backwards.
    '
    ' NO TRIGONOMETRY. PDF has no arc operator, so a pie's elliptical arcs
    ' become cubic Béziers -- and the control points for a circular arc follow
    ' from its endpoints with nothing but `sqrt`, while splitting a wide arc is
    ' vector bisection. gBASIC has no trig builtins and `chart` carries its own
    ' Taylor sine; a second copy here would be a second thing to drift.

    function svg(doc, svg_text, x, y, options)
        opts = _options(options, [ "width", "height" ], "gpdf.svg")
        src = string(svg_text)
        head = match(src, "<svg[^>]*>")
        if is_unknown(head) then
            error "gpdf.svg: this does not start with an <svg> element"
        end if
        battrs = _attrs(head.text)
        native_w = _numattr(battrs, "width", 0)
        native_h = _numattr(battrs, "height", 0)
        if native_w <= 0 or native_h <= 0 then
            error "gpdf.svg: the <svg> element declares no usable width and height"
        end if
        w = _default(opts, "width", native_w)
        h = _default(opts, "height", native_h)
        sx = w / native_w
        sy = h / native_h

        out = doc
        if count(out.pages) = 0 then
            error "gpdf.svg: there is no page yet -- call gpdf.add_page first"
        end if
        ' `y` is the BOTTOM-left of the box, as everywhere else in this library.
        top = y + h
        body = mid(src, len(head.text), len(src) - len(head.text))

        for each m in match_all(body, "<([a-z]+)([^>]*)>")
            kind = m.groups[0]
            a = _attrs(m.groups[1])
            if kind = "line" then
                out = _svg_line(out, a, x, top, sx, sy)
            end if
            if kind = "rect" then
                out = _svg_rect(out, a, x, top, sx, sy)
            end if
            if kind = "circle" then
                out = _svg_circle(out, a, x, top, sx, sy)
            end if
            if kind = "path" then
                out = _svg_path(out, a, x, top, sx, sy)
            end if
            if kind = "text" then
                label = _svg_text_of(body, m)
                out = _svg_text(out, a, label, x, top, sx, sy, battrs)
            end if
            if not contains([ "line", "rect", "circle", "path", "text", "svg" ], kind) then
                error ("gpdf.svg: <" + kind + "> is outside the subset this translates " +
                       "(line, rect, circle, path, text). It is refused rather than dropped, " +
                       "so a chart that grew a new element says so instead of losing part of itself.")
            end if
        end for
        return out
    end function

    ' ---- the pieces ------------------------------------------------------

    function _svg_line(doc, a, ox, top, sx, sy)
        col = _default(a, "stroke", "#000000")
        if col = "none" then return doc
        out = _stroke_colour(doc, col)
        out = _append(out, (_num(_numattr(a, "stroke-width", 1) * sx) + " w " +
                            _pt(ox + _numattr(a, "x1", 0) * sx, top - _numattr(a, "y1", 0) * sy) + " m " +
                            _pt(ox + _numattr(a, "x2", 0) * sx, top - _numattr(a, "y2", 0) * sy) + " l S"))
        return out
    end function

    function _svg_rect(doc, a, ox, top, sx, sy)
        rx = ox + _numattr(a, "x", 0) * sx
        rw = _numattr(a, "width", 0) * sx
        rh = _numattr(a, "height", 0) * sy
        ' SVG gives the TOP edge; PDF's rectangle operator takes the bottom.
        ry = top - _numattr(a, "y", 0) * sy - rh
        fill = _default(a, "fill", "#000000")
        if fill = "none" then return doc
        out = _fill_colour(doc, fill)
        return _append(out, _pt(rx, ry) + " " + _num(rw) + " " + _num(rh) + " re f")
    end function

    ' A circle is four Béziers. 0.5523 is the standard control-point ratio for
    ' a quarter turn, 4/3 * (sqrt(2) - 1), and is written out rather than
    ' derived so the constant is visible.
    function _svg_circle(doc, a, ox, top, sx, sy)
        fill = _default(a, "fill", "#000000")
        if fill = "none" then return doc
        cx = ox + _numattr(a, "cx", 0) * sx
        cy = top - _numattr(a, "cy", 0) * sy
        r = _numattr(a, "r", 0) * sx
        k = r * 0.5522847498307936
        out = _fill_colour(doc, fill)
        out = _append(out, _pt(cx + r, cy) + " m")
        out = _append(out, _pt(cx + r, cy + k) + " " + _pt(cx + k, cy + r) + " " + _pt(cx, cy + r) + " c")
        out = _append(out, _pt(cx - k, cy + r) + " " + _pt(cx - r, cy + k) + " " + _pt(cx - r, cy) + " c")
        out = _append(out, _pt(cx - r, cy - k) + " " + _pt(cx - k, cy - r) + " " + _pt(cx, cy - r) + " c")
        out = _append(out, _pt(cx + k, cy - r) + " " + _pt(cx + r, cy - k) + " " + _pt(cx + r, cy) + " c f")
        return out
    end function

    function _svg_text(doc, a, label, ox, top, sx, sy, battrs)
        if label = "" then return doc
        size = _numattr(a, "font-size", _numattr(battrs, "font-size", 12)) * sy
        font = doc.font
        if _default(a, "font-weight", "") = "bold" then
            font = _bold_of(font)
        end if
        out = set_font(doc, font, size)
        out = _fill_colour(out, _default(a, "fill", "#000000"))
        tx = ox + _numattr(a, "x", 0) * sx
        ty = top - _numattr(a, "y", 0) * sy
        ' SVG anchors text; PDF does not. The offset is MEASURED with the same
        ' core-14 widths everything else uses -- which is why the metrics layer
        ' had to come first for this to be possible at all.
        anchor = _default(a, "text-anchor", "start")
        w = text_width(font, size, label)
        if anchor = "middle" then tx = tx - w / 2
        if anchor = "end" then tx = tx - w
        ' `dominant-baseline: middle` centres on the x-height; 0.36em is the
        ' usual approximation and is what a viewer does with these charts.
        if _default(a, "dominant-baseline", "") = "middle" then
            ty = ty - size * 0.36
        end if
        out = _draw(out, tx, ty, label)
        return set_font(out, doc.font, doc.font_size)
    end function

    ' The text between <text ...> and </text>, unescaped. `chart` escapes its
    ' labels, so a title containing `R&D <script>` arrives here as entities.
    function _svg_text_of(body, m)
        after = m.start + m.length
        close = find(mid(body, after, len(body) - after), "</text>")
        if is_unknown(close) then return ""
        raw = mid(body, after, close)
        raw = replace(raw, "&lt;", "<")
        raw = replace(raw, "&gt;", ">")
        raw = replace(raw, "&quot;", chr(34))
        raw = replace(raw, "&#39;", "'")
        return replace(raw, "&amp;", "&")
    end function

    ' ---- paths -----------------------------------------------------------

    ' The subset `chart` emits: M, L and A, absolute, plus Z. Anything else is
    ' refused by name for the same reason an unknown element is.
    function _svg_path(doc, a, ox, top, sx, sy)
        d = _default(a, "d", "")
        if d = "" then return doc
        stroke = _default(a, "stroke", "none")
        fill = _default(a, "fill", "none")
        out = doc
        if fill != "none" then out = _fill_colour(out, fill)
        if stroke != "none" then
            out = _stroke_colour(out, stroke)
            out = _append(out, _num(_numattr(a, "stroke-width", 1) * sx) + " w")
        end if

        ' TOKENIZED, NOT SPLIT ON SPACES. SVG lets a command letter sit hard
        ' against its first number -- `M31.6 196.4` is what `chart` actually
        ' emits -- so splitting on whitespace yields the token `M31.6`, which
        ' is neither a command nor a number.
        toks = []
        ' ANY letter is a token, not just the supported ones: a regex that
        ' matched only M/L/A/Z would skip a `C` silently and then blame the
        ' number after it, reporting the wrong cause for the right problem.
        for each tk in match_all(d, "[A-Za-z]|-?[0-9]+\\.?[0-9]*")
            append(toks, tk.text)
        end for
        i = 0
        cur = [ 0, 0 ]
        closed = false
        ops = 0
        while i < count(toks)
            t = toks[i]
            if t = "M" or t = "L" then
                px = ox + number(toks[i + 1]) * sx
                py = top - number(toks[i + 2]) * sy
                verb = " m"
                if t = "L" then verb = " l"
                out = _append(out, _pt(px, py) + verb)
                cur = [ px, py ]
                ops = ops + 1
                i = i + 3
            else
                if t = "A" then
                    r = number(toks[i + 1]) * sx
                    large = number(toks[i + 4])
                    sweep = number(toks[i + 5])
                    ex = ox + number(toks[i + 6]) * sx
                    ey = top - number(toks[i + 7]) * sy
                    out = _arc(out, cur, [ ex, ey ], r, large, sweep)
                    cur = [ ex, ey ]
                    ops = ops + 1
                    i = i + 8
                else
                    if t = "Z" or t = "z" then
                        closed = true
                        i = i + 1
                    else
                        if t = "" then
                            i = i + 1
                        else
                            error ("gpdf.svg: path command '" + t + "' is outside the subset " +
                                   "this translates (M, L, A, Z)")
                        end if
                    end if
                end if
            end if
        end while
        if closed then out = _append(out, "h")
        paint = "S"
        if fill != "none" and stroke != "none" then paint = "B"
        if fill != "none" and stroke = "none" then paint = "f"
        if ops > 0 then out = _append(out, paint)
        return out
    end function

    ' A circular arc as cubic Béziers, WITH NO TRIGONOMETRY.
    '
    ' The centre is found from the two endpoints and the radius; the control
    ' points from the half-angle, whose sine and cosine come out of the DOT
    ' PRODUCT of the two unit radii by the half-angle identities -- so `sqrt`
    ' is the only function needed. A span wider than 90 degrees is BISECTED,
    ' and bisecting is vector arithmetic: the midpoint of an arc is the
    ' normalised sum of its endpoints (negated for the long way round).
    function _arc(doc, p0, p1, r, large, sweep)
        dx = p1[0] - p0[0]
        dy = p1[1] - p0[1]
        chord = sqrt(dx * dx + dy * dy)
        if chord = 0 or r <= 0 then
            return _append(doc, _pt(p1[0], p1[1]) + " l")
        end if
        if r < chord / 2 then r = chord / 2
        ' Distance from the chord's midpoint to the centre.
        hh = r * r - (chord / 2) * (chord / 2)
        if hh < 0 then hh = 0
        hd = sqrt(hh)
        mx = (p0[0] + p1[0]) / 2
        my = (p0[1] + p1[1]) / 2
        ' Perpendicular to the chord. Which side the centre sits on is decided
        ' by the two flags together -- in PDF's y-up space the SVG sweep is
        ' reversed, which is the one place the coordinate flip is visible in
        ' logic rather than arithmetic.
        ux = -dy / chord
        uy = dx / chord
        side = -1
        if large = sweep then side = 1
        cx = mx + side * hd * ux
        cy = my + side * hd * uy
        v1 = [ (p0[0] - cx) / r, (p0[1] - cy) / r ]
        v2 = [ (p1[0] - cx) / r, (p1[1] - cy) / r ]
        return _arc_span(doc, cx, cy, r, v1, v2, sweep, large)
    end function

    function _arc_span(doc, cx, cy, r, v1, v2, sweep, large)
        dot = v1[0] * v2[0] + v1[1] * v2[1]
        if large = 1 or dot < 0 then
            m = _arc_mid(v1, v2, sweep, large)
            out = _arc_span(doc, cx, cy, r, v1, m, sweep, 0)
            return _arc_span(out, cx, cy, r, m, v2, sweep, 0)
        end if
        return _arc_one(doc, cx, cy, r, v1, v2, sweep)
    end function

    ' The midpoint of the arc from v1 to v2. For the short way it is the
    ' normalised sum; for the long way, the opposite point. When the two are
    ' exactly opposed the sum is zero and the perpendicular is used instead.
    function _arc_mid(v1, v2, sweep, large)
        sx = v1[0] + v2[0]
        sy = v1[1] + v2[1]
        n = sqrt(sx * sx + sy * sy)
        if n < 0.000001 then
            px = 0 - v1[1]
            py = v1[0]
            if sweep = 1 then
                px = v1[1]
                py = 0 - v1[0]
            end if
            return [ px, py ]
        end if
        s = 1
        if large = 1 then s = -1
        return [ s * sx / n, s * sy / n ]
    end function

    ' One Bézier for a span of at most 90 degrees. k = 4/3 * (1 - cos t) / sin t
    ' at the HALF angle t, and both come from the dot product by the half-angle
    ' identities -- no trigonometry anywhere.
    function _arc_one(doc, cx, cy, r, v1, v2, sweep)
        dot = v1[0] * v2[0] + v1[1] * v2[1]
        if dot > 1 then dot = 1
        if dot < 0 - 1 then dot = 0 - 1
        cos_half = sqrt((1 + dot) / 2)
        sin_half = sqrt((1 - dot) / 2)
        if sin_half < 0.000001 then
            return _append(doc, _pt(cx + v2[0] * r, cy + v2[1] * r) + " l")
        end if
        k = (4 / 3) * (1 - cos_half) / sin_half
        ' The tangent direction at each end, turned the way the arc sweeps.
        dir = 1
        cross = v1[0] * v2[1] - v1[1] * v2[0]
        if cross < 0 then dir = 0 - 1
        p1x = cx + r * (v1[0] - dir * k * v1[1])
        p1y = cy + r * (v1[1] + dir * k * v1[0])
        p2x = cx + r * (v2[0] + dir * k * v2[1])
        p2y = cy + r * (v2[1] - dir * k * v2[0])
        ex = cx + v2[0] * r
        ey = cy + v2[1] * r
        return _append(doc, _pt(p1x, p1y) + " " + _pt(p2x, p2y) + " " + _pt(ex, ey) + " c")
    end function

    ' ---- small helpers ---------------------------------------------------

    function _append(doc, op)
        out = doc
        i = count(out.pages) - 1
        out.pages[i].content = out.pages[i].content + op + chr(10)
        return out
    end function

    function _pt(x, y)
        return _num(x) + " " + _num(y)
    end function

    function _attrs(text)
        a = {}
        for each m in match_all(text, "([a-zA-Z0-9-]+)=\"([^\"]*)\"")
            a[m.groups[0]] = m.groups[1]
        end for
        return a
    end function

    function _numattr(a, name, fallback)
        if not has(a, name) then return fallback
        v = trim(string(a[name]))
        if v = "" then return fallback
        on error goto next
        n = number(v)
        if error then
            error.clear()
            return fallback
        end if
        on error stop
        return n
    end function

    ' "#rrggbb" -> a PDF colour operator. PDF takes components in 0..1.
    function _colour_ops(c, op)
        h = replace(string(c), "#", "")
        if len(h) = 3 then
            h = mid(h, 0, 1) + mid(h, 0, 1) + mid(h, 1, 1) + mid(h, 1, 1) + mid(h, 2, 1) + mid(h, 2, 1)
        end if
        if len(h) != 6 then
            error "gpdf.svg: '" + string(c) + "' is not a #rrggbb colour"
        end if
        b = hex_decode(h)
        return (_num3(byte_at(b, 0) / 255) + " " + _num3(byte_at(b, 1) / 255) + " " +
                _num3(byte_at(b, 2) / 255) + " " + op)
    end function

    function _fill_colour(doc, c)
        return _append(doc, _colour_ops(c, "rg"))
    end function

    function _stroke_colour(doc, c)
        return _append(doc, _colour_ops(c, "RG"))
    end function

    ' Colours need more places than coordinates: 1/255 rounded to two decimals
    ' banks visibly on a gradient.
    function _num3(n)
        r = floor(n * 1000 + 0.5) / 1000
        if r = floor(r) then return string(floor(r))
        return string(r)
    end function

    ' ---- page numbers ----------------------------------------------------

    ' "Page 3 of 7" needs a total nobody knows until the document is finished,
    ' which in a streaming writer means two passes or patching bytes. Here the
    ' pages are VALUES in an array, so stamping them at the end is exact and
    ' costs nothing.
    function number_pages(doc, options)
        opts = _options(options, [ "format", "font", "size", "y", "align" ], "gpdf.number_pages")
        fmt = _default(opts, "format", "Page {n} of {total}")
        font = _default(opts, "font", doc.font)
        size = _default(opts, "size", 9)
        align = _default(opts, "align", "center")
        total = count(doc.pages)
        if total = 0 then
            error "gpdf.number_pages: this document has no pages"
        end if
        out = set_font(doc, font, size)
        saved = out.cursor
        i = 0
        while i < total
            label = replace(replace(fmt, "{n}", string(i + 1)), "{total}", string(total))
            w = text_width(font, size, label)
            pg = out.pages[i]
            y = _default(opts, "y", out.margin / 2)
            x = out.margin
            if align = "center" then
                x = (pg.size.width - w) / 2
            end if
            if align = "right" then
                x = pg.size.width - out.margin - w
            end if
            ' _draw appends to the LAST page, so the target page is moved to
            ' the end, stamped, and put back -- pages are values, so this is a
            ' swap rather than a mutation anything else can see.
            keep = out.pages
            out.pages = [ pg ]
            out = _draw(out, x, y, label)
            stamped = out.pages[0]
            keep[i] = stamped
            out.pages = keep
            i = i + 1
        end while
        out.cursor = saved
        return set_font(out, doc.font, doc.font_size)
    end function

    ' ---- serialization ---------------------------------------------------

    ' A number as PDF writes them: no exponent, and no trailing noise from a
    ' double. PDF has no exponent notation at all, so `string(1e-7)` would be
    ' syntactically invalid inside a content stream.
    function _num(n)
        r = floor(n * 100 + 0.5) / 100
        if r = floor(r) then
            return string(floor(r))
        end if
        return string(r)
    end function

    ' ( ) and \ are the three bytes that end or escape a PDF string literal.
    function _escape(s)
        out = replace(s, "\\", "\\\\")
        out = replace(out, "(", "\\(")
        out = replace(out, ")", "\\)")
        return out
    end function

    function _font_index(doc, name)
        i = 0
        while i < count(doc.fonts_used)
            if doc.fonts_used[i] = name then return i + 1
            i = i + 1
        end while
        return 1
    end function

    ' The whole document as bytes.
    '
    ' THE CROSS-REFERENCE TABLE IS WHY THIS FUNCTION IS ONE PIECE: every entry
    ' is the BYTE OFFSET of an object from the start of the file, and
    ' `startxref` is the byte offset of the table itself. Both are accumulated
    ' with `byte_count` as the body is built, never with `len`, which counts
    ' codepoints and would be wrong for every offset after the first accented
    ' character.
    function render(doc)
        if count(doc.pages) = 0 then
            error "gpdf.render: this document has no pages"
        end if
        fonts = doc.fonts_used
        if count(fonts) = 0 then fonts = [ "Helvetica" ]

        npages = count(doc.pages)
        ' Object numbering: 1 catalog, 2 pages, then per page a page object and
        ' a contents object, then the fonts, then info.
        first_page_obj = 3
        font_obj = first_page_obj + npages * 2
        info_obj = font_obj + count(fonts)

        objs = []
        append(objs, "<< /Type /Catalog /Pages 2 0 R >>")

        kids = ""
        i = 0
        while i < npages
            kids = kids + string(first_page_obj + i * 2) + " 0 R"
            if i < npages - 1 then kids = kids + " "
            i = i + 1
        end while
        append(objs, "<< /Type /Pages /Count " + string(npages) + " /Kids [" + kids + "] >>")

        i = 0
        while i < npages
            pg = doc.pages[i]
            fres = ""
            j = 0
            while j < count(fonts)
                fres = fres + "/F" + string(j + 1) + " " + string(font_obj + j) + " 0 R"
                if j < count(fonts) - 1 then fres = fres + " "
                j = j + 1
            end while
            append(objs, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 " +
                         _num(pg.size.width) + " " + _num(pg.size.height) + "]" +
                         " /Resources << /Font << " + fres + " >> >>" +
                         " /Contents " + string(first_page_obj + i * 2 + 1) + " 0 R >>")
            stream = pg.content
            filter = ""
            if doc.compress then
                stream = compress(stream)
                filter = " /Filter /FlateDecode"
            end if
            append(objs, "<< /Length " + string(byte_count(stream)) + filter + " >>" + chr(10) +
                         "stream" + chr(10) + stream + chr(10) + "endstream")
            i = i + 1
        end while

        for each f in fonts
            append(objs, "<< /Type /Font /Subtype /Type1 /BaseFont /" + f +
                         " /Encoding /WinAnsiEncoding >>")
        end for

        info = "<< /Producer (gBASIC gpdf)"
        if doc.title != "" then info = info + " /Title (" + _escape(to_winansi(doc.title, "gpdf.render")) + ")"
        if doc.author != "" then info = info + " /Author (" + _escape(to_winansi(doc.author, "gpdf.render")) + ")"
        if doc.subject != "" then info = info + " /Subject (" + _escape(to_winansi(doc.subject, "gpdf.render")) + ")"
        if doc.creator != "" then info = info + " /Creator (" + _escape(to_winansi(doc.creator, "gpdf.render")) + ")"
        ' NO CLOCK. A document given no `created` carries no CreationDate at
        ' all, which is honest; inventing one would make the bytes unstable and
        ' a golden meaningless.
        if not is_unknown(doc.created) then
            info = info + " /CreationDate (D:" + _pdf_date(doc.created) + ")"
        end if
        info = info + " >>"
        append(objs, info)

        ' The binary comment on line 2 tells any tool that moves this file that
        ' it is not text. Four bytes above 127, as the specification advises.
        out = "%PDF-1.4" + chr(10) + "%" + from_bytes([0xE2, 0xE3, 0xCF, 0xD3]) + chr(10)
        offsets = []
        i = 0
        while i < count(objs)
            append(offsets, byte_count(out))
            out = out + string(i + 1) + " 0 obj" + chr(10) + objs[i] + chr(10) + "endobj" + chr(10)
            i = i + 1
        end while

        xref_at = byte_count(out)
        n = count(objs) + 1
        out = out + "xref" + chr(10) + "0 " + string(n) + chr(10)
        out = out + "0000000000 65535 f " + chr(10)
        for each off in offsets
            out = out + _pad10(off) + " 00000 n " + chr(10)
        end for
        out = out + ("trailer" + chr(10) + "<< /Size " + string(n) +
                     " /Root 1 0 R /Info " + string(info_obj) + " 0 R >>" + chr(10))
        out = out + "startxref" + chr(10) + string(xref_at) + chr(10) + "%%EOF" + chr(10)
        return out
    end function

    ' Each xref entry is exactly 20 bytes: 10 digits, a space, 5 digits, a
    ' space, one letter, then a two-byte end of line. A reader seeks by
    ' multiplying, so a short line moves every entry after it.
    function _pad10(n)
        s = string(floor(n))
        while len(s) < 10
            s = "0" + s
        end while
        return s
    end function

    function _pdf_date(dt)
        s = string(dt)
        ' "2026-09-13 14:30:00" -> "20260913143000"
        out = ""
        i = 0
        while i < len(s)
            c = mid(s, i, 1)
            if c >= "0" and c <= "9" then out = out + c
            i = i + 1
        end while
        return left(out + "00000000000000", 14) + "Z"
    end function

    function save(doc, path)
        f {file}= path
        write(f, render(doc))
        return bytes(f)
    end function

    ' ---- options ---------------------------------------------------------

    function _options(rec, known, label)
        if is_unknown(rec) then
            return {}
        end if
        if not is_record(rec) then
            error label + " expects its options as a record"
        end if
        for each k in keys(rec)
            if not contains(known, k) then
                error label + ": unknown option '" + k + "' (known: " + join(known, ", ") + ")"
            end if
        end for
        return rec
    end function

    function _default(rec, field, fallback)
        if has(rec, field) then
            return rec[field]
        end if
        return fallback
    end function

end library
