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
        if count(doc.pages) = 0 then
            error "gpdf.text: there is no page yet -- call gpdf.add_page first"
        end if
        enc = to_winansi(s, "gpdf.text")
        out = doc
        i = count(out.pages) - 1
        body = ("BT /F" + string(_font_index(out, out.font)) + " " + _num(out.font_size) +
                " Tf 1 0 0 1 " + _num(x) + " " + _num(y) + " Tm (" + _escape(enc) + ") Tj ET" + chr(10))
        out.pages[i].content = out.pages[i].content + body
        out.cursor = { x: doc.cursor.x, y: y - out.font_size * 1.2 }
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
