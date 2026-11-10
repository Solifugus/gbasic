' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' gpdf -- PDF documents, written from the specification.
'
' PHASE 1: the document, the core-14 fonts, measured text and word wrap.
' Then tables, images, SVG charts, and (2026-11-10, book gap G58) TrueType
' fonts embedded subset to the glyphs drawn. Written clean from ISO 32000 rather than ported from anything, so the
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
'    swallowing Japanese text) and will not ship it. Embedded fonts have
'    landed and the refusal narrowed, as planned, with nothing a caller wrote
'    having to change: in a font from gpdf.font_file, what is refused is a
'    character it has no glyph for, or one from a script needing shaping.
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
                 images: [],
                 font: "Helvetica",
                 font_size: 12,
                 fonts_used: [],
                 embedded: {},
                 glyphs: {} }
    end function

    ' A page is a content stream plus the state a caller cares about. Pages are
    ' VALUES in an array, like every other collection here, so `add_page`
    ' returns the new document rather than mutating one -- a function cannot
    ' write back through its argument, and an API that appeared to would
    ' silently do nothing.
    function add_page(doc)
        out = doc
        append(out.pages, { content: "", size: doc.size, images: [] })
        out.cursor = { x: doc.margin, y: doc.size.height - doc.margin }
        return out
    end function

    function page_count(doc)
        return count(doc.pages)
    end function

    ' ---- fonts and measurement -------------------------------------------

    ' `font` is a core-14 name, a font value from gpdf.font_file, or the name
    ' of a font value this document was already given.
    function set_font(doc, font, size)
        if not is_number(size) or size <= 0 then
            error "gpdf.set_font: size must be a positive number of points"
        end if
        out = doc
        name = font
        if is_record(font) then
            if _default(font, "kind", "") != "truetype" then
                error "gpdf.set_font: a font record must be one gpdf.font_file returned"
            end if
            name = font.name
            if has(out.embedded, name) then
                if out.embedded[name].file != font.file or byte_count(out.embedded[name].raw) != byte_count(font.raw) then
                    error ("gpdf.set_font: this document already has a font called '" + name + "' (" +
                           out.embedded[name].file + "), and " + font.file + " is another; give one of " +
                           "them another name with gpdf.font_file's `name:`")
                end if
            else
                out.embedded[name] = font
                out.glyphs[name] = { cid_of: {}, gids: [ 0 ], text: [ "" ] }
            end if
        else
            if is_unknown(gpdf_metrics.widths(name)) and not has(out.embedded, string(name)) then
                error ("gpdf.set_font: '" + string(name) + "' is not a core-14 font (" +
                       join(gpdf_metrics.fonts(), ", ") + ") or a font this document was given. " +
                       "A TrueType font is read with gpdf.font_file(path).")
            end if
        end if
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
    ' caller's UTF-8. `font` is a core-14 name or a font value from
    ' gpdf.font_file, whose widths are its glyphs' advances.
    function text_width(font, size, text)
        if is_record(font) then
            s = string(text)
            n = len(s)
            total = 0
            i = 0
            while i < n
                ch = mid(s, i, 1)
                known = font.common[ch]
                if is_unknown(known) then
                    total = total + _advance(font, _glyph_or_refuse(font, ch, i, n, "gpdf.text_width"))
                else
                    total = total + known[1]
                end if
                i = i + 1
            end while
            return total * size / font.upem
        end if
        w = gpdf_metrics.widths(font)
        if is_unknown(w) then
            error ("gpdf.text_width: '" + string(font) + "' is not a core-14 font; for an " +
                   "embedded one pass the value gpdf.font_file returned")
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

    ' A name in a document resolved to what measures it: the font value for an
    ' embedded font, the name itself for a core-14 one.
    function _face(doc, font)
        if is_string(font) and has(doc.embedded, font) then
            return doc.embedded[font]
        end if
        return font
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
                       ") cannot be written in WinAnsi, the core-14 fonts' encoding. " +
                       "It is character " + string(i + 1) + " of " + string(n) + ". A " +
                       "TrueType font that has it can be embedded (gpdf.font_file); nothing " +
                       "is substituted, because a wrong character on a posted document is " +
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
        digits = "0123456789ABCDEF"
        h = ""
        v = n
        while v > 0
            h = mid(digits, mod(v, 16), 1) + h
            v = floor(v / 16)
        end while
        while len(h) < 4
            h = "0" + h
        end while
        return h
    end function

    ' Can every character be written? Answers a BOOLEAN rather than raising, so
    ' an application can check a customer's name at the point it is entered
    ' instead of at the point a document is produced.
    function representable(text, font = "Helvetica")
        s = string(text)
        if is_record(font) then
            i = 0
            while i < len(s)
                cp = code(mid(s, i, 1))
                if cp < 32 or (cp >= 127 and cp < 160) or _needs_shaping(cp) then return false
                g = _glyph(font, cp)
                if g = 0 or g >= font.glyphs then return false
                i = i + 1
            end while
            return true
        end if
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

    ' ---- embedded fonts (book gap G58, 2026-11-10) ------------------------
    '
    ' A TrueType font read from a file, so a document can write what WinAnsi
    ' cannot: a customer in Łódź, İzmir or Новосибирск. The font goes into the
    ' PDF SUBSET to the glyphs the document drew, as a CID font (Type0 over
    ' CIDFontType2, Identity-H) whose CIDs are the subset's own glyph numbers,
    ' with a ToUnicode map so the text extracts and searches as written.
    '
    ' THE REFUSALS KEEP THEIR SHAPE. A character the font has no glyph for is
    ' refused by name, as WinAnsi's are, and so is one from a script that
    ' needs shaping (Arabic joins, Hebrew runs right to left, Indic scripts
    ' reorder): drawn one glyph at a time those print a wrong word, which is
    ' the failure the refusal exists to prevent. Kerning is not applied, so
    ' a measured width is the sum of the advances, which is what is drawn.
    '
    ' Fonts with PostScript (CFF) outlines, usually `.otf`, are refused by
    ' name: this phase subsets TrueType outlines only.

    ' gpdf.font_file(path, options) -> a font value, read once and usable in
    ' any number of documents: `gpdf.set_font(d, f, 11)`. Options: `name`, the
    ' name a document knows it by (default the font's PostScript name), and
    ' `index`, which font of a collection (.ttc) to read (default 0).
    function font_file(path, options = unknown)
        opts = _options(options, [ "name", "index" ], "gpdf.font_file")
        whose = "gpdf.font_file: " + string(path)
        f {file}= path
        raw = read(f)
        size = byte_count(raw)
        if size < 12 then
            error whose + " is " + string(size) + " bytes, too short to be a font"
        end if
        at = 0
        if byte_slice(raw, 0, 4) = "ttcf" then
            n = unpack(">I", raw, 8)[0]
            idx = _default(opts, "index", 0)
            if not is_number(idx) or idx != floor(idx) or idx < 0 or idx >= n then
                error whose + " is a collection of " + string(n) + " fonts; `index` must be a whole number from 0 to " + string(n - 1)
            end if
            at = unpack(">I", raw, 12 + 4 * idx)[0]
        else
            if has(opts, "index") then
                error whose + " holds one font, not a collection (.ttc), so it takes no `index`"
            end if
        end if
        kind = byte_slice(raw, at, 4)
        if kind = "OTTO" then
            error (whose + " has PostScript (CFF) outlines; this phase embeds fonts with " +
                   "TrueType outlines, usually a .ttf file")
        end if
        if kind != from_bytes([0, 1, 0, 0]) and kind != "true" then
            error whose + " is not a TrueType or OpenType font (it begins " + hex_encode(byte_slice(raw, 0, 4)) + ")"
        end if
        ntables = unpack(">H", raw, at + 4)[0]
        if at + 12 + 16 * ntables > size then
            error whose + " is cut short: its table directory runs past the end of the file"
        end if
        tables = {}
        i = 0
        while i < ntables
            r = unpack(">4s I I I", raw, at + 12 + 16 * i)
            if r[2] + r[3] > size then
                error whose + " is cut short: its '" + r[0] + "' table runs past the end of the file"
            end if
            tables[r[0]] = { offset: r[2], length: r[3] }
            i = i + 1
        end while
        if not has(tables, "glyf") and has(tables, "CFF ") then
            error (whose + " has PostScript (CFF) outlines; this phase embeds fonts with " +
                   "TrueType outlines, usually a .ttf file")
        end if
        for each t in [ "head", "hhea", "maxp", "hmtx", "loca", "glyf", "cmap" ]
            if not has(tables, t) then
                error whose + " has no '" + t + "' table, which a TrueType font needs"
            end if
        end for

        h = tables.head.offset
        if unpack(">I", raw, h + 12)[0] != 0x5F0F3CF5 then
            error whose + ": its 'head' table does not carry the TrueType magic number"
        end if
        upem = unpack(">H", raw, h + 18)[0]
        bbox = unpack(">h h h h", raw, h + 36)
        long_loca = unpack(">h", raw, h + 50)[0] = 1
        hh = tables.hhea.offset
        ascent = unpack(">h", raw, hh + 4)[0]
        descent = unpack(">h", raw, hh + 6)[0]
        nhm = unpack(">H", raw, hh + 34)[0]
        nglyphs = unpack(">H", raw, tables.maxp.offset + 4)[0]
        if upem = 0 or nhm = 0 or nhm > nglyphs then
            error (whose + ": its head, hhea and maxp tables disagree (" + string(upem) + " units per em, " +
                   string(nhm) + " metrics for " + string(nglyphs) + " glyphs)")
        end if

        cap = ascent
        weight = 400
        if has(tables, "OS/2") then
            o = tables["OS/2"].offset
            fs_type = unpack(">H", raw, o + 8)[0]
            ' fsType: 0x0002 is "restricted licence embedding", which forbids
            ' putting the font in a document at all; 0x0200 is bitmap only.
            if band(fs_type, 0x0002) != 0 and band(fs_type, 0x000C) = 0 then
                error whose + ": its licence forbids embedding it (OS/2 fsType " + string(fs_type) + ", restricted)"
            end if
            if band(fs_type, 0x0200) != 0 then
                error whose + ": its licence allows embedding its bitmaps only (OS/2 fsType " + string(fs_type) + ")"
            end if
            weight = unpack(">H", raw, o + 4)[0]
            if unpack(">H", raw, o)[0] >= 2 and tables["OS/2"].length >= 90 then
                cap = unpack(">h", raw, o + 88)[0]
            end if
        end if
        italic = 0
        fixed = false
        if has(tables, "post") then
            p = tables.post.offset
            italic = unpack(">i", raw, p + 4)[0] / 65536
            fixed = unpack(">I", raw, p + 12)[0] != 0
        end if

        name = _default(opts, "name", _ps_name(raw, tables))
        if not is_string(name) or name = "" then
            error whose + " names no PostScript name; give it one with `name:`"
        end if
        if not is_unknown(gpdf_metrics.widths(name)) then
            error (whose + " is called '" + name + "', which is a core-14 font's name; " +
                   "give it another with `name:` so the two cannot be confused")
        end if
        font = { kind: "truetype", name: name, file: string(path), raw: raw,
                 tables: tables, upem: upem, bbox: bbox, ascent: ascent,
                 descent: descent, cap_height: cap, italic: italic, fixed: fixed,
                 weight: weight, glyphs: nglyphs, long_loca: long_loca,
                 metrics: nhm, cmap: _cmap(raw, tables.cmap, whose), common: {} }
        ' The glyph and advance of each character of the commonest scripts
        ' (Latin through Extended-A, Greek, Cyrillic), found once here rather
        ' than by a search per character drawn or measured: a 1000-row table
        ' took 5.9 s against Helvetica's 1.55 s before this, 2026-11-10.
        common = {}
        for each r in [ [ 0x20, 0x7E ], [ 0xA0, 0x17F ], [ 0x370, 0x3FF ], [ 0x400, 0x4FF ] ]
            cp = r[0]
            while cp <= r[1]
                g = _glyph(font, cp)
                if g != 0 and g < nglyphs then
                    common[chr(cp)] = [ g, _advance(font, g) ]
                end if
                cp = cp + 1
            end while
        end for
        font.common = common
        return font
    end function

    ' The PostScript name (name ID 6), as PDF wants a name: letters, digits
    ' and - or _ only. Mac Roman (platform 1) is bytes; Windows (platform 3)
    ' is UTF-16BE, whose ASCII is every second byte.
    function _ps_name(raw, tables)
        if not has(tables, "name") then return ""
        at = tables.name.offset
        hdr = unpack(">H H H", raw, at)
        i = 0
        while i < hdr[1]
            r = unpack(">H H H H H H", raw, at + 6 + 12 * i)
            if r[3] = 6 then
                s = byte_slice(raw, at + hdr[2] + r[5], r[4])
                stride = 1
                first = 0
                if r[0] = 3 or r[0] = 0 then
                    stride = 2
                    first = 1
                end if
                out = ""
                j = first
                while j < byte_count(s)
                    b = byte_at(s, j)
                    if (b >= 48 and b <= 57) or (b >= 65 and b <= 90) or (b >= 97 and b <= 122) or b = 45 or b = 95 then
                        out = out + from_bytes([b])
                    end if
                    j = j + stride
                end while
                if out != "" then return out
            end if
            i = i + 1
        end while
        return ""
    end function

    ' The Unicode character map, chosen and read once: format 12 (all of
    ' Unicode) if the font has one, else format 4 (the Basic Multilingual
    ' Plane). Glyphs are looked up from it per character, never expanded
    ' into a table of every character the font has.
    function _cmap(raw, t, whose)
        at = t.offset
        n = unpack(">H", raw, at + 2)[0]
        best = -1
        best_rank = 0
        i = 0
        while i < n
            r = unpack(">H H I", raw, at + 4 + 8 * i)
            sub = at + r[2]
            fmt = unpack(">H", raw, sub)[0]
            unicode = r[0] = 0 or (r[0] = 3 and (r[1] = 1 or r[1] = 10))
            rank = 0
            if unicode and fmt = 12 then rank = 2
            if unicode and fmt = 4 then rank = 1
            if rank > best_rank then
                best = sub
                best_rank = rank
            end if
            i = i + 1
        end while
        if best < 0 then
            error whose + " has no Unicode character map (cmap format 4 or 12), so no character can be found in it"
        end if
        if best_rank = 2 then
            ngroups = unpack(">I", raw, best + 12)[0]
            g = unpack(">" + string(3 * ngroups) + "I", raw, best + 16)
            starts = []
            ends = []
            gids = []
            i = 0
            while i < ngroups
                append(starts, g[3 * i])
                append(ends, g[3 * i + 1])
                append(gids, g[3 * i + 2])
                i = i + 1
            end while
            return { format: 12, starts: starts, ends: ends, gids: gids }
        end if
        segs = unpack(">H", raw, best + 6)[0] / 2
        ends = unpack(">" + string(segs) + "H", raw, best + 14)
        starts = unpack(">" + string(segs) + "H", raw, best + 16 + 2 * segs)
        deltas = unpack(">" + string(segs) + "H", raw, best + 16 + 4 * segs)
        ranges_at = best + 16 + 6 * segs
        ranges = unpack(">" + string(segs) + "H", raw, ranges_at)
        return { format: 4, starts: starts, ends: ends, deltas: deltas,
                 ranges: ranges, ranges_at: ranges_at }
    end function

    ' The glyph for codepoint `cp`, or 0 (the font's "missing" glyph).
    function _glyph(font, cp)
        m = font.cmap
        lo = 0
        hi = count(m.ends) - 1
        while lo < hi
            mid_at = floor((lo + hi) / 2)
            if m.ends[mid_at] < cp then
                lo = mid_at + 1
            else
                hi = mid_at
            end if
        end while
        if lo > hi or cp < m.starts[lo] or cp > m.ends[lo] then return 0
        if m.format = 12 then
            return m.gids[lo] + cp - m.starts[lo]
        end if
        if m.ranges[lo] = 0 then
            return mod(cp + m.deltas[lo], 65536)
        end if
        addr = m.ranges_at + 2 * lo + m.ranges[lo] + 2 * (cp - m.starts[lo])
        if addr + 2 > byte_count(font.raw) then return 0
        g = unpack(">H", font.raw, addr)[0]
        if g = 0 then return 0
        return mod(g + m.deltas[lo], 65536)
    end function

    ' A glyph's advance in font units.
    function _advance(font, gid)
        i = gid
        if i >= font.metrics then i = font.metrics - 1
        return unpack(">H", font.raw, font.tables.hmtx.offset + 4 * i)[0]
    end function

    ' Scripts drawn wrongly one glyph at a time, left to right: joined
    ' (Arabic, Syriac, N'Ko), right to left (Hebrew, Thaana), or reordered
    ' and shaped (the Indic scripts, Tibetan, Myanmar, Khmer and their
    ' neighbours).
    function _needs_shaping(cp)
        if cp < 0x0590 then return false
        for each r in [ [0x0590, 0x08FF], [0x0900, 0x0DFF], [0x0F00, 0x109F],
                        [0x1780, 0x18AF], [0x1900, 0x1AFF], [0x1B00, 0x1C4F],
                        [0xA800, 0xA8FF], [0xA980, 0xAAFF], [0xABC0, 0xABFF],
                        [0xFB1D, 0xFDFF], [0xFE70, 0xFEFF], [0x10A00, 0x10A5F],
                        [0x11000, 0x1124F], [0x1E900, 0x1E95F] ]
            if cp >= r[0] and cp <= r[1] then return true
        end for
        return false
    end function

    ' The glyph for one character in an embedded font, or a refusal naming it.
    function _glyph_or_refuse(font, ch, i, n, whose)
        known = font.common[ch]
        if not is_unknown(known) then return known[0]
        cp = code(ch)
        where = " It is character " + string(i + 1) + " of " + string(n) + "."
        if cp < 32 or (cp >= 127 and cp < 160) then
            error whose + ": character U+" + _u(cp) + " is a control character, which no font draws." + where
        end if
        if _needs_shaping(cp) then
            error (whose + ": '" + ch + "' (U+" + _u(cp) + ") is from a script whose letters join, " +
                   "reorder or run right to left, which needs shaping that gpdf does not do; " +
                   "drawn one glyph at a time it would print a wrong word." + where)
        end if
        g = _glyph(font, cp)
        if g = 0 or g >= font.glyphs then
            error (whose + ": '" + ch + "' (U+" + _u(cp) + ") is not in the font " + font.name +
                   " (" + font.file + "); nothing is substituted." + where)
        end if
        return g
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
        out = doc
        shown = ""
        if has(out.embedded, out.font) then
            ' Two bytes per glyph, the subset's own glyph number (its CID),
            ' given in order of first use. The character each stands for is
            ' kept for the ToUnicode map.
            font = out.embedded[out.font]
            usage = out.glyphs[out.font]
            str = string(s)
            n = len(str)
            hex = []
            k = 0
            while k < n
                ch = mid(str, k, 1)
                g = _glyph_or_refuse(font, ch, k, n, "gpdf.text")
                key = string(g)
                if not has(usage.cid_of, key) then
                    usage.cid_of[key] = count(usage.gids)
                    append(usage.gids, g)
                    append(usage.text, ch)
                end if
                append(hex, pack(">H", [ usage.cid_of[key] ]))
                k = k + 1
            end while
            out.glyphs[out.font] = usage
            shown = "<" + upper(hex_encode(join(hex, ""))) + ">"
        else
            shown = "(" + _escape(to_winansi(s, "gpdf.text")) + ")"
        end if
        i = count(out.pages) - 1
        body = ("BT /F" + string(_font_index(out, out.font)) + " " + _num(out.font_size) +
                " Tf 1 0 0 1 " + _num(x) + " " + _num(y) + " Tm " + shown + " Tj ET" + chr(10))
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
                        candidate = word
                        if cur != "" then candidate = cur + " " + word
                        if text_width(font, size, candidate) <= width then
                            cur = candidate
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
        for each line in wrap(_face(out, out.font), out.font_size, width, s)
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
                lines = wrap(_face(out, body_font), body_size, c.width - 2 * pad, txt)
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
                w = text_width(_face(out, out.font), out.font_size, line)
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

    ' ---- images ----------------------------------------------------------
    '
    ' A logo on an invoice, which is the whole requirement. PNG and JPEG are
    ' embedded BY COPYING THEIR BYTES: a non-interlaced PNG's pixel data is
    ' already a zlib stream and a JPEG is already DCT, and PDF speaks both --
    ' /FlateDecode with a PNG predictor, and /DCTDecode. So nothing is decoded,
    ' nothing is re-encoded, and a photograph costs what it costs on disk.
    '
    ' WHAT IS REFUSED IS REFUSED BY NAME: an interlaced PNG and one with an
    ' alpha channel. Both need the pixels actually decoded -- Adam7 is a
    ' different layout, and alpha has to be split into a separate soft mask --
    ' and a library that quietly dropped the transparency would put a black box
    ' where a logo should be.

    function image(doc, path, x, y, options)
        opts = _options(options, [ "width", "height" ], "gpdf.image")
        f {file}= path
        if not exists(f) then
            error "gpdf.image: there is no file at '" + string(path) + "'"
        end if
        raw = read(f)
        info = _image_info(raw, path)

        w = _default(opts, "width", unknown)
        h = _default(opts, "height", unknown)
        ' Natural size is the pixel count as points, and giving one dimension
        ' keeps the aspect ratio -- a logo squashed because only its width was
        ' set is the commonest thing to get wrong here.
        if is_unknown(w) and is_unknown(h) then
            w = info.width
            h = info.height
        end if
        if is_unknown(w) then w = h * info.width / info.height
        if is_unknown(h) then h = w * info.height / info.width

        out = doc
        if count(out.pages) = 0 then
            error "gpdf.image: there is no page yet -- call gpdf.add_page first"
        end if
        ' One entry per distinct file, so the same logo on forty pages is
        ' stored once.
        idx = -1
        i = 0
        while i < count(out.images)
            if out.images[i].key = string(path) then idx = i
            i = i + 1
        end while
        if idx < 0 then
            info.key = string(path)
            append(out.images, info)
            idx = count(out.images) - 1
        end if
        pi = count(out.pages) - 1
        if not contains(out.pages[pi].images, idx) then
            append(out.pages[pi].images, idx)
        end if
        ' `cm` scales the unit square the image is drawn into, so the matrix is
        ' the size and the position together.
        out = _append(out, "q " + _num(w) + " 0 0 " + _num(h) + " " + _pt(x, y) +
                           " cm /Im" + string(idx + 1) + " Do Q")
        return out
    end function

    function _image_info(raw, path)
        if byte_count(raw) > 8 and hex_encode(byte_slice(raw, 0, 8)) = "89504e470d0a1a0a" then
            return _png_info(raw, path)
        end if
        if byte_count(raw) > 3 and hex_encode(byte_slice(raw, 0, 2)) = "ffd8" then
            return _jpeg_info(raw, path)
        end if
        error ("gpdf.image: '" + string(path) + "' is neither a PNG nor a JPEG. Those are " +
               "the two formats PDF can carry without decoding, which is why they are the two this takes.")
    end function



    function _be32(s, at)
        return (byte_at(s, at) * 16777216 + byte_at(s, at + 1) * 65536 +
                byte_at(s, at + 2) * 256 + byte_at(s, at + 3))
    end function

    function _png_info(raw, path)
        n = byte_count(raw)
        i = 8
        width = 0
        height = 0
        bits = 8
        ctype = 0
        palette = ""
        data = ""
        while i + 8 <= n
            ln = _be32(raw, i)
            typ = ""
            k = 0
            while k < 4
                typ = typ + from_bytes([ byte_at(raw, i + 4 + k) ])
                k = k + 1
            end while
            body_at = i + 8
            if typ = "IHDR" then
                width = _be32(raw, body_at)
                height = _be32(raw, body_at + 4)
                bits = byte_at(raw, body_at + 8)
                ctype = byte_at(raw, body_at + 9)
                if byte_at(raw, body_at + 12) != 0 then
                    error ("gpdf.image: '" + string(path) + "' is an INTERLACED PNG. Adam7 stores " +
                           "the pixels in seven passes, which PDF cannot read directly; save it " +
                           "without interlacing.")
                end if
                if ctype = 4 or ctype = 6 then
                    error ("gpdf.image: '" + string(path) + "' has an ALPHA CHANNEL (PNG colour " +
                           "type " + string(ctype) + "). Carrying it needs the pixels decoded and " +
                           "the alpha split into a soft mask, which this phase does not do -- and " +
                           "dropping it silently would put a black box where the transparency was.")
                end if
            end if
            if typ = "PLTE" then
                palette = byte_slice(raw, body_at, ln)
            end if
            if typ = "IDAT" then
                ' EVERY IDAT, concatenated. A PNG of any size has several --
                ' the gBASIC mascot has six -- and taking only the first gives
                ' a truncated zlib stream that fails at the reader, not here.
                data = data + byte_slice(raw, body_at, ln)
            end if
            if typ = "IEND" then
                i = n
            else
                i = body_at + ln + 4
            end if
        end while
        if width = 0 or height = 0 or data = "" then
            error "gpdf.image: '" + string(path) + "' is not a PNG this can read (no IHDR or no IDAT)"
        end if
        colours = 1
        space = "/DeviceGray"
        if ctype = 2 then
            colours = 3
            space = "/DeviceRGB"
        end if
        if ctype = 3 then
            if palette = "" then
                error "gpdf.image: '" + string(path) + "' is a palette PNG with no PLTE chunk"
            end if
            hival = floor(byte_count(palette) / 3) - 1
            space = "[/Indexed /DeviceRGB " + string(hival) + " <" + hex_encode(palette) + ">]"
        end if
        return { kind: "png", width: width, height: height, bits: bits,
                 space: space, colours: colours, data: data,
                 filter: "/FlateDecode",
                 parms: ("<< /Predictor 15 /Colors " + string(colours) + " /BitsPerComponent " +
                         string(bits) + " /Columns " + string(width) + " >>") }
    end function



    ' JPEG: walk the markers to the frame header, which carries the size and
    ' the component count. The entropy-coded data is copied whole.
    function _jpeg_info(raw, path)
        n = byte_count(raw)
        i = 2
        while i + 4 <= n
            if byte_at(raw, i) != 255 then
                error "gpdf.image: '" + string(path) + "' is a malformed JPEG (expected a marker)"
            end if
            m = byte_at(raw, i + 1)
            ' SOF0/1/2/3 and the arithmetic-coded variants all lay the frame
            ' header out the same way; SOF2 is PROGRESSIVE, which PDF readers
            ' do accept, so it is allowed through.
            if (m >= 192 and m <= 195) or (m >= 197 and m <= 199) or (m >= 201 and m <= 203) then
                h = byte_at(raw, i + 5) * 256 + byte_at(raw, i + 6)
                w = byte_at(raw, i + 7) * 256 + byte_at(raw, i + 8)
                comps = byte_at(raw, i + 9)
                space = "/DeviceRGB"
                if comps = 1 then space = "/DeviceGray"
                if comps = 4 then space = "/DeviceCMYK"
                if comps != 1 and comps != 3 and comps != 4 then
                    error "gpdf.image: '" + string(path) + "' has " + string(comps) + " colour components"
                end if
                return { kind: "jpeg", width: w, height: h, bits: 8,
                         space: space, colours: comps, data: raw,
                         filter: "/DCTDecode", parms: "" }
            end if
            seg = byte_at(raw, i + 2) * 256 + byte_at(raw, i + 3)
            i = i + 2 + seg
        end while
        error "gpdf.image: '" + string(path) + "' is a JPEG with no frame header"
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
        w = text_width(_face(out, font), size, label)
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
            w = text_width(_face(out, out.font), size, label)
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
        ' a contents object, then the fonts, then the images, then info.
        first_page_obj = 3
        font_obj = first_page_obj + npages * 2
        ' A core font is one object; an embedded one is five (see _embedded).
        font_refs = []
        font_objs = []
        next_obj = font_obj
        for each f in fonts
            append(font_refs, next_obj)
            if has(doc.embedded, f) then
                for each o in _embedded(doc, f, next_obj)
                    append(font_objs, o)
                    next_obj = next_obj + 1
                end for
            else
                append(font_objs, "<< /Type /Font /Subtype /Type1 /BaseFont /" + f +
                                  " /Encoding /WinAnsiEncoding >>")
                next_obj = next_obj + 1
            end if
        end for
        image_obj = next_obj
        info_obj = image_obj + count(doc.images)

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
                fres = fres + "/F" + string(j + 1) + " " + string(font_refs[j]) + " 0 R"
                if j < count(fonts) - 1 then fres = fres + " "
                j = j + 1
            end while
            ' Only the images THIS page uses are named in its resources; a
            ' page that names one it never draws still makes a reader load it.
            xres = ""
            for each ix in pg.images
                xres = xres + "/Im" + string(ix + 1) + " " + string(image_obj + ix) + " 0 R "
            end for
            if xres != "" then
                xres = " /XObject << " + trim(xres) + " >>"
            end if
            append(objs, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 " +
                         _num(pg.size.width) + " " + _num(pg.size.height) + "]" +
                         " /Resources << /Font << " + fres + " >>" + xres + " >>" +
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

        for each o in font_objs
            append(objs, o)
        end for

        ' Each image is an XObject whose stream is the file's own compressed
        ' pixel data, copied rather than re-encoded.
        for each im in doc.images
            parms = ""
            if im.parms != "" then parms = " /DecodeParms " + im.parms
            append(objs, "<< /Type /XObject /Subtype /Image /Width " + string(im.width) +
                         " /Height " + string(im.height) + " /ColorSpace " + im.space +
                         " /BitsPerComponent " + string(im.bits) + " /Filter " + im.filter +
                         parms + " /Length " + string(byte_count(im.data)) + " >>" + chr(10) +
                         "stream" + chr(10) + im.data + chr(10) + "endstream")
        end for

        info = "<< /Producer (gBASIC gpdf)"
        if doc.title != "" then info = info + " /Title " + _info_text(doc.title)
        if doc.author != "" then info = info + " /Author " + _info_text(doc.author)
        if doc.subject != "" then info = info + " /Subject " + _info_text(doc.subject)
        if doc.creator != "" then info = info + " /Creator " + _info_text(doc.creator)
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

    ' A document-information string. Info strings are PDFDocEncoding, NOT
    ' WinAnsi: the two agree on ASCII and Latin-1 but not on 0x80-0x9F, where
    ' WinAnsi's Euro is PDFDoc's bullet, and PDFDoc leaves 0xAD undefined.
    ' Until 2026-11-10 titles went as WinAnsi, so "Price €9 — net" showed
    ' in a reader's document properties as "Price •9 Š net" (measured with
    ' pdfinfo). Those bytes, and anything WinAnsi cannot write, now go as
    ' UTF-16BE with its byte order mark, which is how PDF writes any script.
    function _info_text(s)
        t = string(s)
        if representable(t) then
            enc = to_winansi(t, "gpdf.render")
            high = false
            i = 0
            while i < byte_count(enc)
                b = byte_at(enc, i)
                if (b >= 128 and b < 160) or b = 173 then high = true
                i = i + 1
            end while
            if not high then return "(" + _escape(enc) + ")"
        end if
        return "<FEFF" + upper(hex_encode(transcode(t, "utf-8", "utf-16be"))) + ">"
    end function

    ' The five objects an embedded font is, numbered from `first`: the Type0
    ' font a page names, its CIDFontType2 descendant with every glyph's width,
    ' the font descriptor, the subset font program, and the ToUnicode map.
    function _embedded(doc, name, first)
        font = doc.embedded[name]
        usage = doc.glyphs[name]
        sub = _subset(font, usage.gids)
        tag = _subset_tag(sub.gids)
        base = tag + "+" + font.name
        scale = 1000 / font.upem

        widths = []
        for each g in sub.gids
            append(widths, _num(_advance(font, g) * scale))
        end for

        flags = 4
        if font.fixed then flags = flags + 1
        if font.italic != 0 then flags = flags + 64
        fd = ("<< /Type /FontDescriptor /FontName /" + base + " /Flags " + string(flags) +
              " /FontBBox [" + _num(font.bbox[0] * scale) + " " + _num(font.bbox[1] * scale) + " " +
              _num(font.bbox[2] * scale) + " " + _num(font.bbox[3] * scale) + "]" +
              " /ItalicAngle " + _num(font.italic) + " /Ascent " + _num(font.ascent * scale) +
              " /Descent " + _num(font.descent * scale) + " /CapHeight " + _num(font.cap_height * scale) +
              " /StemV " + string(_stem_v(font.weight)) + " /FontFile2 " + string(first + 3) + " 0 R >>")

        return [ "<< /Type /Font /Subtype /Type0 /BaseFont /" + base + " /Encoding /Identity-H" +
                 " /DescendantFonts [" + string(first + 1) + " 0 R] /ToUnicode " + string(first + 4) + " 0 R >>",
                 "<< /Type /Font /Subtype /CIDFontType2 /BaseFont /" + base +
                 " /CIDSystemInfo << /Registry (Adobe) /Ordering (Identity) /Supplement 0 >>" +
                 " /FontDescriptor " + string(first + 2) + " 0 R /CIDToGIDMap /Identity" +
                 " /W [0 [" + join(widths, " ") + "]] >>",
                 fd,
                 _stream(doc, sub.program, " /Length1 " + string(byte_count(sub.program))),
                 _stream(doc, _to_unicode(usage.text), "") ]
    end function

    function _stream(doc, data, extra)
        body = data
        filter = ""
        if doc.compress then
            body = compress(data)
            filter = " /Filter /FlateDecode"
        end if
        return ("<< /Length " + string(byte_count(body)) + filter + extra + " >>" + chr(10) +
                "stream" + chr(10) + body + chr(10) + "endstream")
    end function

    ' The usual estimate of a stem's width from the weight class; readers use
    ' it only to embolden or hint, and a font program carries no such number.
    function _stem_v(weight)
        return floor(10 + 220 * (weight - 50) / 900 + 0.5)
    end function

    ' Six capital letters naming this subset, as PDF requires, taken from the
    ' glyphs it holds: the same glyphs give the same tag, so the bytes stay
    ' reproducible, and two different subsets of one font differ.
    function _subset_tag(gids)
        h = 7
        for each g in gids
            h = mod(h * 31 + g + 1, 308915776)
        end for
        out = ""
        i = 0
        while i < 6
            out = out + from_bytes([ 65 + mod(h, 26) ])
            h = floor(h / 26)
            i = i + 1
        end while
        return out
    end function

    ' CID -> the character it was drawn for, so text extracts as written.
    function _to_unicode(text)
        lines = [ "/CIDInit /ProcSet findresource begin", "12 dict begin", "begincmap",
                  "/CIDSystemInfo << /Registry (Adobe) /Ordering (UCS) /Supplement 0 >> def",
                  "/CMapName /Adobe-Identity-UCS def", "/CMapType 2 def",
                  "1 begincodespacerange", "<0000> <FFFF>", "endcodespacerange" ]
        entries = []
        i = 1
        while i < count(text)
            append(entries, "<" + upper(hex_encode(pack(">H", [ i ]))) + "> <" +
                            upper(hex_encode(transcode(text[i], "utf-8", "utf-16be"))) + ">")
            i = i + 1
        end while
        ' At most 100 entries to a block, as the CMap format requires.
        at = 0
        while at < count(entries)
            n = count(entries) - at
            if n > 100 then n = 100
            append(lines, string(n) + " beginbfchar")
            j = 0
            while j < n
                append(lines, entries[at + j])
                j = j + 1
            end while
            append(lines, "endbfchar")
            at = at + n
        end while
        for each l in [ "endcmap", "CMapName currentdict /CMap defineresource pop", "end", "end" ]
            append(lines, l)
        end for
        return join(lines, chr(10)) + chr(10)
    end function

    ' ---- the subset font program -----------------------------------------
    '
    ' A new TrueType font holding only the glyphs drawn, renumbered in order of
    ' first use (glyph 0 stays the missing glyph), plus every glyph a drawn
    ' COMPOSITE glyph is built from: 'ł' in many fonts is an 'l' and a stroke,
    ' and leaving the 'l' out prints only the stroke. The tables are the nine
    ' ISO 32000 names for an embedded TrueType program (9.9): glyf, head,
    ' hhea, hmtx, loca, maxp and, where the font has them, cvt, fpgm and prep,
    ' which the hinting instructions need. cmap is not among them: the
    ' document reaches glyphs by number (CIDToGIDMap /Identity).
    function _subset(font, drawn)
        raw = font.raw
        gids = []
        new_of = {}
        for each g in drawn
            new_of[string(g)] = count(gids)
            append(gids, g)
        end for
        ' Components are added as they are found, so the walk reaches a
        ' component's own components too.
        datas = []
        k = 0
        while k < count(gids)
            data = _glyph_data(font, gids[k])
            if byte_count(data) >= 10 and unpack(">h", data, 0)[0] < 0 then
                at = 10
                more = true
                while more
                    c = unpack(">H H", data, at)
                    key = string(c[1])
                    if not has(new_of, key) then
                        if c[1] >= font.glyphs then
                            error ("gpdf.render: " + font.file + " is damaged: glyph " + string(gids[k]) +
                                   " is built from glyph " + string(c[1]) + ", and the font has " +
                                   string(font.glyphs))
                        end if
                        new_of[key] = count(gids)
                        append(gids, c[1])
                    end if
                    data = byte_slice(data, 0, at + 2) + pack(">H", [ new_of[key] ]) + byte_slice(data, at + 4)
                    size = 4
                    if band(c[0], 0x0001) != 0 then
                        size = size + 4
                    else
                        size = size + 2
                    end if
                    if band(c[0], 0x0008) != 0 then size = size + 2
                    if band(c[0], 0x0040) != 0 then size = size + 4
                    if band(c[0], 0x0080) != 0 then size = size + 8
                    at = at + size
                    more = band(c[0], 0x0020) != 0
                end while
            end if
            append(datas, data)
            k = k + 1
        end while

        glyf = []
        loca = []
        hmtx = []
        off = 0
        k = 0
        while k < count(gids)
            d = datas[k]
            while mod(byte_count(d), 4) != 0
                d = d + from_bytes([ 0 ])
            end while
            append(loca, off)
            append(glyf, d)
            off = off + byte_count(d)
            append(hmtx, pack(">H h", [ _advance(font, gids[k]), _lsb(font, gids[k]) ]))
            k = k + 1
        end while
        append(loca, off)

        t = font.tables
        n = count(gids)
        head = byte_slice(raw, t.head.offset, 54)
        head = byte_slice(head, 0, 8) + pack(">I", [ 0 ]) + byte_slice(head, 12, 38) + pack(">h h", [ 1, 0 ])
        hhea = byte_slice(raw, t.hhea.offset, 36)
        hhea = byte_slice(hhea, 0, 34) + pack(">H", [ n ])
        maxp = byte_slice(raw, t.maxp.offset, t.maxp.length)
        maxp = byte_slice(maxp, 0, 4) + pack(">H", [ n ]) + byte_slice(maxp, 6)

        parts = { head: head, hhea: hhea, maxp: maxp,
                  loca: pack(">" + string(count(loca)) + "I", loca),
                  glyf: join(glyf, ""), hmtx: join(hmtx, "") }
        for each extra in [ "cvt ", "fpgm", "prep" ]
            if has(t, extra) then
                parts[extra] = byte_slice(raw, t[extra].offset, t[extra].length)
            end if
        end for
        return { gids: gids, program: _sfnt(parts) }
    end function

    ' One glyph's outline bytes, empty for a glyph with no outline (a space).
    function _glyph_data(font, g)
        t = font.tables
        if font.long_loca then
            o = unpack(">I I", font.raw, t.loca.offset + 4 * g)
        else
            o = unpack(">H H", font.raw, t.loca.offset + 2 * g)
            o = [ o[0] * 2, o[1] * 2 ]
        end if
        if o[1] <= o[0] then return ""
        if o[1] > t.glyf.length then
            error "gpdf.render: " + font.file + " is damaged: glyph " + string(g) + " runs past its glyf table"
        end if
        return byte_slice(font.raw, t.glyf.offset + o[0], o[1] - o[0])
    end function

    ' A glyph's left side bearing: from its full metric, or from the bare
    ' bearings that follow the metrics for the glyphs past numberOfHMetrics.
    function _lsb(font, g)
        at = font.tables.hmtx.offset
        if g < font.metrics then
            return unpack(">h", font.raw, at + 4 * g + 2)[0]
        end if
        return unpack(">h", font.raw, at + 4 * font.metrics + 2 * (g - font.metrics))[0]
    end function

    ' A font file from its tables: the directory, tables in tag order each
    ' padded to four bytes, every table's checksum, and head's adjustment,
    ' which makes the whole file sum to 0xB1B0AFBA.
    function _sfnt(parts)
        tags = sort(keys(parts))
        n = count(tags)
        p2 = 1
        e = 0
        while p2 * 2 <= n
            p2 = p2 * 2
            e = e + 1
        end while
        dir = [ pack(">I H H H H", [ 0x00010000, n, p2 * 16, e, n * 16 - p2 * 16 ]) ]
        body = []
        off = 12 + 16 * n
        for each tag in tags
            d = parts[tag]
            len_d = byte_count(d)
            while mod(byte_count(d), 4) != 0
                d = d + from_bytes([ 0 ])
            end while
            append(dir, pack(">4s I I I", [ tag, _checksum(d), off, len_d ]))
            append(body, d)
            off = off + byte_count(d)
        end for
        file = join(dir, "") + join(body, "")
        adjust = mod(0xB1B0AFBA - _checksum(file), 4294967296)
        ' The adjustment goes in at byte 8 of head, found by adding up the
        ' padded tables before it.
        at = 12 + 16 * n
        for each tag in tags
            if tag = "head" then break
            at = at + byte_count(parts[tag]) + mod(4 - mod(byte_count(parts[tag]), 4), 4)
        end for
        return byte_slice(file, 0, at + 8) + pack(">I", [ adjust ]) + byte_slice(file, at + 12)
    end function

    ' The sum of a table's big-endian 32-bit words, modulo 2^32. `d` is
    ' already a multiple of four bytes long.
    function _checksum(d)
        total = 0
        at = 0
        while at < byte_count(d)
            n = floor((byte_count(d) - at) / 4)
            if n > 4096 then n = 4096
            for each w in unpack(">" + string(n) + "I", d, at)
                total = total + w
            end for
            total = mod(total, 4294967296)
            at = at + 4 * n
        end while
        return total
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

    ' Writes the document and returns nothing (2026-11-09). It returned the
    ' bytes written, so the ordinary statement `gpdf.save(d, path)` drew
    ' unused-result warning 2101, which explains a dropped UPDATE: that warning
    ' is right for add_page and set_font, and noise here, so callers wrote
    ' `n = gpdf.save(...)` to quiet it (DOGFOOD row 15). The size is
    ' file_size(path), exactly, and a failed write raises.
    function save(doc, path)
        f {file}= path
        write(f, render(doc))
        return nothing
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
