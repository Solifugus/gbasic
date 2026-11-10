' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' `pdf` -- reading a PDF: its pages, their words with positions, and the
' pictures on them (docs/pdf_read_design.md; book gap G54).
'
' PURE gBASIC OVER POPPLER'S PROGRAMS (pdfinfo, pdftotext, pdftohtml,
' pdfimages), as `ocr` is over tesseract's. poppler is GPL and gBASIC is
' Apache-2.0, so it is run, never linked; and what it does -- every font
' encoding, damaged cross-reference tables rebuilt -- is years of work to
' repeat. A native reader could replace this later without moving the values.
'
' EVERY PAGE SAYS HOW IT WAS READ (Matthew, 2026-11-09). A PDF's own text is
' what its producer wrote; OCR is an engine's guess with a confidence. A
' program that cannot tell them apart takes a 70%-confident total as fact. So
' a page is `text`, `scan`, `ocr` or `blank`, and OCR runs only when asked
' (`ocr: true`): it is also seconds a page where text is milliseconds.
'
' FAILURE TO READ A FILE IS A VALUE, `ok: false` with poppler's own words: a
' damaged attachment is an ordinary case in a supplier's mailbox. A wrong
' PROGRAM -- poppler missing, an unknown option, a page out of range -- raises.
library pdf

load xml
load ocr

' ---------------------------------------------------------------------------
' What is installed
' ---------------------------------------------------------------------------

function _run(cmd, args)
    return process.run({ command: cmd, args: args, launch_failure: "result" })
end function

' poppler-utils is one package; pdftotext stands for it.
function available()
    r = _run("pdftotext", ["-v"])
    return not r.launch_failed and r.exit_code = 0
end function

function _need(verb)
    if not available() then
        error ("pdf." + verb + ": poppler's programs are not installed or not on PATH "
               + "(the poppler-utils package: pdfinfo, pdftotext, pdftohtml, pdfimages)")
    end if
    return nothing
end function

function _first_line(t)
    for each l in split(t, chr(10))
        if trim(l) != "" then
            return trim(l)
        end if
    end for
    return "poppler gave no reason"
end function

function _after(l, label)
    return trim(mid(l, len(label), len(l)))
end function

' ---------------------------------------------------------------------------
' The document
' ---------------------------------------------------------------------------

' `pdf.info(path)` -> { ok, why, pages, title, producer, creator, encrypted,
' sizes: [ { width, height, rotation } ] }, sizes in points, one per page.
function info(path)
    _need("info")
    p = string(path)
    r = _run("pdfinfo", [p])
    if r.exit_code != 0 then
        return { ok: false, why: _first_line(r.stderr), pages: 0, sizes: [] }
    end if
    out = { ok: true, why: "", pages: 0, title: "", producer: "", creator: "",
            encrypted: false, sizes: [] }
    for each l in split(r.stdout, chr(10))
        if starts_with(l, "Pages:") then
            out.pages = number(_after(l, "Pages:"))
        else if starts_with(l, "Title:") then
            out.title = _after(l, "Title:")
        else if starts_with(l, "Producer:") then
            out.producer = _after(l, "Producer:")
        else if starts_with(l, "Creator:") then
            out.creator = _after(l, "Creator:")
        else if starts_with(l, "Encrypted:") then
            out.encrypted = starts_with(_after(l, "Encrypted:"), "yes")
        end if
    end for
    if out.pages = 0 then
        return out
    end if
    ' each page's own size and rotation, which may differ page to page
    s = _run("pdfinfo", ["-f", "1", "-l", string(out.pages), p])
    sizes = {}
    rots = {}
    for each l in split(s.stdout, chr(10))
        m = match(l, "^Page +([0-9]+) size: +([0-9.]+) x ([0-9.]+) pts")
        if not is_unknown(m) then
            sizes[m.groups[0]] = { width: number(m.groups[1]), height: number(m.groups[2]) }
        end if
        m = match(l, "^Page +([0-9]+) rot: +(-?[0-9]+)")
        if not is_unknown(m) then
            rots[m.groups[0]] = number(m.groups[1])
        end if
    end for
    for i = 1 to out.pages
        k = string(i)
        sz = { width: unknown, height: unknown, rotation: 0 }
        if has(sizes, k) then
            sz.width = sizes[k].width
            sz.height = sizes[k].height
        end if
        if has(rots, k) then
            sz.rotation = rots[k]
        end if
        append(out.sizes, sz)
    next
    return out
end function

' ---------------------------------------------------------------------------
' Options
' ---------------------------------------------------------------------------

function option_names()
    return ["ocr", "languages"]
end function

function _options(given, verb)
    o = { ocr: false, languages: "eng" }
    if is_nothing(given) or is_unknown(given) then
        return o
    end if
    if type(given) != "record" then
        error "pdf." + verb + ": options must be a record, for example { ocr: true }"
    end if
    for each k in keys(given)
        if not contains(option_names(), k) then
            error ("pdf." + verb + ": unknown option '" + k + "' (known: "
                   + join(option_names(), ", ") + ")")
        end if
        o[k] = given[k]
    end for
    if type(o.ocr) != "boolean" then
        error "pdf." + verb + ": ocr is true or false"
    end if
    return o
end function

function _page_number(verb, d, n)
    if type(n) != "number" then
        error "pdf." + verb + ": the page is a number from 1"
    end if
    if n != floor(n) or n < 1 or n > d.pages then
        error ("pdf." + verb + ": there is no page " + string(n) + " (the document has "
               + string(d.pages) + ")")
    end if
    return nothing
end function

' ---------------------------------------------------------------------------
' Words, from the PDF's own text
' ---------------------------------------------------------------------------

' pdftotext -bbox-layout writes XHTML: page / flow / block / line / word, each
' word with its box in points from the top-left corner. A flow and a block
' together are `ocr`'s block, so `ocr.grid` lays a text page out as it lays out
' a scan.
function _text_words(p, n)
    r = _run("pdftotext", ["-f", string(n), "-l", string(n), "-bbox-layout", p, "-"])
    if r.exit_code != 0 then
        error "pdf: pdftotext could not read page " + string(n) + ": " + _first_line(r.stderr)
    end if
    doc = xml.parse(r.stdout)
    words = []
    pages = xml.find_all(doc, "body/doc/page")
    if count(pages) = 0 then
        return words
    end if
    bi = 0
    for each flow in xml.find_all(pages[0], "flow")
        for each block in xml.find_all(flow, "block")
            li = 0
            for each ln in xml.find_all(block, "line")
                for each w in xml.find_all(ln, "word")
                    x0 = number(xml.attr(w, "xMin"))
                    y0 = number(xml.attr(w, "yMin"))
                    append(words, { text: xml.text(w),
                                    left: x0, top: y0,
                                    width: number(xml.attr(w, "xMax")) - x0,
                                    height: number(xml.attr(w, "yMax")) - y0,
                                    block: bi, paragraph: 0, line: li,
                                    confidence: unknown })
                end for
                li = li + 1
            end for
            bi = bi + 1
        end for
    end for
    return words
end function

' ---------------------------------------------------------------------------
' Pictures
' ---------------------------------------------------------------------------

' pdftohtml -xml at zoom 1 places each picture in points; the file it writes
' is read for its pixel size, and removed.
function _images(p, n)
    d = temp_dir()
    r = _run("pdftohtml", ["-xml", "-zoom", "1", "-q", "-f", string(n), "-l", string(n),
                           "-stdout", p, join_path(d, "img")])
    if r.exit_code != 0 then
        remove_tree(d)
        error "pdf: pdftohtml could not read page " + string(n) + ": " + _first_line(r.stderr)
    end if
    out = []
    for each l in split(r.stdout, chr(10))
        if starts_with(trim(l), "<image ") then
            im = xml.parse(trim(l))
            px = _png_size(xml.attr(im, "src"))
            append(out, { left: number(xml.attr(im, "left")), top: number(xml.attr(im, "top")),
                          width: number(xml.attr(im, "width")), height: number(xml.attr(im, "height")),
                          pixels_wide: px.wide, pixels_high: px.high })
        end if
    end for
    remove_tree(d)
    return out
end function

function _png_size(path)
    f {file}= path
    if not exists(f) then
        return { wide: unknown, high: unknown }
    end if
    b = read_at(f, 0, 24)
    if byte_count(b) < 24 or hex_encode(byte_slice(b, 0, 8)) != "89504e470d0a1a0a" then
        return { wide: unknown, high: unknown }
    end if
    return { wide: _be32(b, 16), high: _be32(b, 20) }
end function

function _be32(s, at)
    return (byte_at(s, at) * 16777216 + byte_at(s, at + 1) * 65536 +
            byte_at(s, at + 2) * 256 + byte_at(s, at + 3))
end function

' A picture the size of the page covers at least nine tenths of it each way,
' as placed (Chapter 18's measure).
function _page_sized(im, width, height)
    return im.width >= width * 0.9 and im.height >= height * 0.9
end function

' ---------------------------------------------------------------------------
' A page
' ---------------------------------------------------------------------------

' `pdf.read_page(path, n [, options])` -> { ok, why, n, how, width, height,
' words, images, text_layer, text_layer_set_aside, skew }. `how` is text,
' scan, ocr or blank. Words and pictures are in points from the top-left.
function read_page(path, n, options = nothing)
    _need("read_page")
    o = _options(options, "read_page")
    p = string(path)
    d = info(p)
    if not d.ok then
        return { ok: false, why: d.why, n: n, how: unknown, words: [], images: [] }
    end if
    _page_number("read_page", d, n)
    return _read(p, d, n, o)
end function

function _read(p, d, n, o)
    sz = d.sizes[n - 1]
    page = { ok: true, why: "", n: n, how: "", width: sz.width, height: sz.height,
             rotation: sz.rotation, words: _text_words(p, n), images: _images(p, n),
             text_layer: false, text_layer_set_aside: false, skew: unknown }
    big = 0
    for each im in page.images
        if _page_sized(im, page.width, page.height) then
            big = big + 1
        end if
    end for
    if big > 1 then
        return { ok: false, n: n, how: unknown, words: [], images: page.images,
                 why: "page " + string(n) + " holds " + string(big)
                      + " pictures the size of the page, so which is the page is not decided" }
    end if
    if big = 0 then
        if count(page.words) > 0 then
            page.how = "text"
        else if count(page.images) > 0 then
            return { ok: false, n: n, how: unknown, words: [], images: page.images,
                     why: "page " + string(n) + " holds no text and only pictures smaller "
                          + "than the page, which is not a page this reads" }
        else
            page.how = "blank"
        end if
        return page
    end if
    ' one picture the size of the page: a scan
    page.text_layer = count(page.words) > 0
    if not o.ocr then
        page.how = "scan"
        return page
    end if
    return _ocr_page(p, n, page, o)
end function

' The page's picture, taken out as it is stored, read by `ocr`, and its words'
' boxes brought from the picture's pixels to the page's points.
function _ocr_page(p, n, page, o)
    if not ocr.available() then
        error "pdf.read_page: ocr: true needs tesseract, which is not installed or not on PATH"
    end if
    d = temp_dir()
    r = _run("pdfimages", ["-f", string(n), "-l", string(n), "-png", p, join_path(d, "scan")])
    shots = files(d)
    if r.exit_code != 0 or count(shots) != 1 then
        remove_tree(d)
        return { ok: false, n: n, how: unknown, words: [], images: page.images,
                 why: "the picture on page " + string(n) + " could not be taken out: "
                      + _first_line(r.stderr) }
    end if
    on error goto next
    q = ocr.read_page(shots[0].path, { languages: o.languages })
    if error then
        why = error.message
        error.clear()
        remove_tree(d)
        return { ok: false, n: n, how: unknown, words: [], images: page.images, why: why }
    end if
    on error stop
    remove_tree(d)
    if not q.ok then
        return { ok: false, n: n, how: unknown, words: [], images: page.images, why: q.why }
    end if
    im = page.images[0]
    for each m in page.images
        if _page_sized(m, page.width, page.height) then
            im = m
        end if
    end for
    sx = im.width / im.pixels_wide
    sy = im.height / im.pixels_high
    words = []
    for each w in q.words
        w.left = im.left + w.left * sx
        w.top = im.top + w.top * sy
        w.width = w.width * sx
        w.height = w.height * sy
        append(words, w)
    end for
    page.text_layer_set_aside = page.text_layer
    page.words = words
    page.skew = q.skew
    page.how = "ocr"
    return page
end function

' `pdf.read(path [, options])` -> { ok, why, pages: [ page ] }: every page, in
' order. A page that could not be read is in the list with its own ok: false.
function read(path, options = nothing)
    _need("read")
    o = _options(options, "read")
    p = string(path)
    d = info(p)
    if not d.ok then
        return { ok: false, why: d.why, pages: [] }
    end if
    pages = []
    for i = 1 to d.pages
        append(pages, _read(p, d, i, o))
    next
    return { ok: true, why: "", pages: pages }
end function

' ---------------------------------------------------------------------------
' Text laid out, and a picture taken out
' ---------------------------------------------------------------------------

' `pdf.text(path, n)` -> the page's own text laid out as printed (poppler's
' -layout). It is the PDF's text only; a scan read by OCR is laid out by
' `ocr.grid(page)` on the page `read_page` gives.
function text(path, n)
    _need("text")
    p = string(path)
    d = info(p)
    if not d.ok then
        error "pdf.text: " + d.why
    end if
    _page_number("text", d, n)
    r = _run("pdftotext", ["-f", string(n), "-l", string(n), "-layout", p, "-"])
    if r.exit_code != 0 then
        error "pdf.text: " + _first_line(r.stderr)
    end if
    return replace(r.stdout, chr(12), "")
end function

' `pdf.image(path, n, index, dir)` -> the path of the page's index-th picture
' (from 0), written into `dir` as it is stored in the PDF (pdfimages -all), not
' drawn again. The order is the PDF's, which may differ from `images`'.
function image(path, n, index, dir)
    _need("image")
    p = string(path)
    d = info(p)
    if not d.ok then
        error "pdf.image: " + d.why
    end if
    _page_number("image", d, n)
    t = temp_dir()
    r = _run("pdfimages", ["-f", string(n), "-l", string(n), "-all", p, join_path(t, "im")])
    if r.exit_code != 0 then
        remove_tree(t)
        error "pdf.image: " + _first_line(r.stderr)
    end if
    got = sort(files(t), { by: "name" })
    if index < 0 or index >= count(got) then
        remove_tree(t)
        error ("pdf.image: page " + string(n) + " has " + string(count(got))
               + " pictures, so there is no picture " + string(index))
    end if
    dest = join_path(string(dir), "page" + string(n) + "_" + string(index) + "." + extension(got[index].path))
    copy(got[index].path, dest)
    remove_tree(t)
    return dest
end function

end library
