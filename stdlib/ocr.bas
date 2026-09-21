' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' `ocr` -- reading text out of an image (docs/ocr_design.md).
'
' PURE gBASIC OVER THE TESSERACT CLI, which is a decision and not a shortcut.
' A native module would need libtesseract and leptonica at build time, and the
' lean release tarball carries no optional modules -- so a native `ocr` would be
' ABSENT FROM THE DOWNLOAD A READER GETS, while this works anywhere `tesseract`
' is installed, which is one package and no rebuild. Every measurement behind
' the design came out of the CLI's own TSV, so the CLI demonstrably gives us
' everything the design needs. If per-page cost ever becomes the complaint, a
' native module replaces the implementation WITHOUT MOVING THE API, because the
' answer is words-with-boxes either way.
'
' ===========================================================================
' WHAT THIS LIBRARY REFUSES TO DO, AND WHY (design §2)
' ===========================================================================
'
' IT DOES NOT RETURN A STRING. `read` answers a PAGE of words, each with its
' box and its confidence. Measured: plain tesseract over an ordinary columnar
' report recognises every word at 90-96% confidence and REORDERS them into
' separate blocks, so an account number and its amount end up twelve lines
' apart -- nothing missing, the association destroyed, and the text still reads
' like a document. Returning that string as the primary answer would make the
' defect the default and discard the evidence needed to detect it.
'
' IT DOES NOT SILENTLY DROP LOW-CONFIDENCE WORDS. A dropped word is
' indistinguishable from a word that was not there -- the distinction `finio`'s
' Axiom 7 already draws between unknown and invalid. A caller may filter; this
' may not.
'
' IT CORRECTS NOTHING. No spell-check, no "0 looks like O in a numeric column",
' no re-reading a field that failed a checksum. Every one of those turns a
' VISIBLE misread into an INVISIBLE one, and the whole value here is that a
' misread can be detected.
library ocr


' ---------------------------------------------------------------------------
' What is installed
' ---------------------------------------------------------------------------

' The languages this machine can read. NOT a static list: a missing language is
' the failure that says nothing (design §1.5), so what a caller needs is what is
' actually present rather than what we hoped for.
function languages()
    r = process.run({ command: "tesseract", args: ["--list-langs"] })
    if r.exit_code != 0 then
        return []
    end if
    out = []
    for each ln in split(r.stdout, chr(10))
        t = trim(ln)
        ' the first line is "List of available languages in ..."
        if len(t) > 0 and not contains(t, " ") then
            append(out, t)
        end if
    end for
    return out
end function

function available()
    r = process.run({ command: "tesseract", args: ["--version"] })
    return r.exit_code = 0
end function

' ---------------------------------------------------------------------------
' Orientation and script (design R0, R0b)
' ---------------------------------------------------------------------------

' MEASURED AS THE DOMINANT REAL-WORLD FAILURE, ahead of everything the synthetic
' work found: of seven hand-held photographs, five were a quarter turn or more
' out, and a 90-degree error is not read as a rotation -- it reads as 87 degrees
' of skew at 30% confidence. EXIF is necessary and NOT sufficient: two of those
' declared `orientation: 1` and were still sideways, because the PAGE was
' sideways inside a correctly-oriented frame, which no metadata can know.
'
' The CONFIDENCE travels because it is weak and the caller has to be able to
' weigh it: measured across those photographs it ranged 0.60 to 26.4, and one
' page it could not orient at all.
function orientation(path)
    r = process.run({ command: "tesseract", args: [string(path), "-", "--psm", "0"] })
    both = r.stdout + chr(10) + r.stderr
    out = { rotate: unknown, confidence: unknown,
            script: unknown, script_confidence: unknown }
    for each ln in split(both, chr(10))
        t = trim(ln)
        if starts_with(t, "Rotate:") then
            out.rotate = number(trim(replace(t, "Rotate:", "")))
        end if
        if starts_with(t, "Orientation confidence:") then
            out.confidence = number(trim(replace(t, "Orientation confidence:", "")))
        end if
        if starts_with(t, "Script:") then
            out.script = trim(replace(t, "Script:", ""))
        end if
        if starts_with(t, "Script confidence:") then
            out.script_confidence = number(trim(replace(t, "Script confidence:", "")))
        end if
    end for
    return out
end function

' ---------------------------------------------------------------------------
' Reading a page
' ---------------------------------------------------------------------------

function default_options()
    return { languages: "eng", psm: 1, min_confidence: 0 }
end function

function option_names()
    return keys(default_options())
end function

function _options(given)
    o = default_options()
    if is_nothing(given) then
        return o
    end if
    if not (type(given) = "record") then
        error "ocr.read: options must be a record"
    end if
    known = keys(o)
    for each k in keys(given)
        if not contains(known, k) then
            error ("ocr.read: unknown option '" + k + "' (known: "
                   + join(known, ", ") + ")")
        end if
        o[k] = given[k]
    end for
    return o
end function

' Turn one TSV line into a word, or `nothing` for the level rows tesseract emits
' for page/block/paragraph/line, which carry no text.
function _word_of(fields)
    if count(fields) < 12 then
        return nothing
    end if
    t = fields[11]
    if len(trim(t)) = 0 then
        return nothing
    end if
    c = number(fields[10])
    if c < 0 then
        return nothing
    end if
    return { text: t,
             confidence: c,
             left: number(fields[6]), top: number(fields[7]),
             width: number(fields[8]), height: number(fields[9]),
             block: number(fields[2]), paragraph: number(fields[3]),
             line: number(fields[4]) }
end function

' THE BOXES COME BACK IN THE ORIGINAL FRAME even when `--psm 1` has rotated the
' page to read it -- measured: a sideways page reads correctly at 78.7%
' confidence while its word boxes still describe a 4080x3060 landscape image and
' a line-fit still reports 81 degrees of "skew". So the boxes are transformed
' here by the rotation tesseract reported, which is pure arithmetic and brings
' the same page to -0.06 degrees.
'
' Without this, `grid` would be laying out a sideways page and would produce a
' plausible document with every row wrong -- the §1.3 failure, manufactured by
' our own library.
function _rotate_boxes(words, rotate, w, h)
    if is_unknown(rotate) or rotate = 0 then
        return words
    end if
    out = []
    for each d in words
        if rotate = 90 then
            append(out, _placed(d, h - (d.top + d.height), d.left, d.height, d.width))
        else
            if rotate = 180 then
                append(out, _placed(d, w - (d.left + d.width), h - (d.top + d.height), d.width, d.height))
            else
                if rotate = 270 then
                    append(out, _placed(d, d.top, w - (d.left + d.width), d.height, d.width))
                else
                    append(out, d)
                end if
            end if
        end if
    end for
    return out
end function

function _placed(d, l, t, w, h)
    n = d
    n.left = l
    n.top = t
    n.width = w
    n.height = h
    return n
end function

' `ocr.read(path [, options])` -> a PAGE.
'
' { ok, words, rotation, rotation_confidence, script, script_confidence,
'   languages, skew, why }
'
' `ok` is about whether the ENGINE RAN, not whether it found anything: a blank
' scan is an ordinary page with no words, and reporting that as a failure would
' make a caller treat an empty page and a broken one the same way.
function read_page(path, options = nothing)
    o = _options(options)
    p = string(path)

    if not available() then
        return { ok: false, why: "tesseract is not installed or not on PATH",
                 words: [], skew: unknown, languages: o.languages }
    end if

    have = languages()
    for each want in split(o.languages, "+")
        if not contains(have, want) then
            ' REFUSED BY NAME, because this is the failure that says nothing:
            ' reading Korean with English data returns 39-82 WORDS at 30-45%
            ' confidence -- not an error, not an empty page, plausible nonsense
            ' of the right general shape. A pipeline checking "did OCR return
            ' text" passes. Measured; installing the language took the same
            ' pages to 8-11x the usable words.
            error ("ocr.read: language '" + want + "' is not installed (have: "
                   + join(have, ", ") + ")")
        end if
    end for

    ori = orientation(p)

    tmp = "/tmp/gbasic_ocr_" + string(random_int(100000, 999999))
    r = process.run({ command: "tesseract",
                      args: [p, tmp, "--psm", string(o.psm), "-l", o.languages, "tsv"] })
    if r.exit_code != 0 then
        return { ok: false, why: trim(r.stderr), words: [],
                 skew: unknown, languages: o.languages }
    end if

    f {file}= tmp + ".tsv"
    body = read(f)
    delete(f)

    words = []
    maxx = 0
    maxy = 0
    first = true
    for each ln in split(body, chr(10))
        if first then
            first = false
        else
            d = _word_of(split(ln, chr(9)))
            if not is_nothing(d) then
                if d.confidence >= o.min_confidence then
                    append(words, d)
                end if
                if d.left + d.width > maxx then maxx = d.left + d.width
                if d.top + d.height > maxy then maxy = d.top + d.height
            end if
        end if
    end for

    words = _rotate_boxes(words, ori.rotate, maxx, maxy)

    return { ok: true,
             words: words,
             rotation: ori.rotate,
             rotation_confidence: ori.confidence,
             script: ori.script,
             script_confidence: ori.script_confidence,
             languages: o.languages,
             skew: skew_of(words) }
end function

' ---------------------------------------------------------------------------
' Skew (design R4)
' ---------------------------------------------------------------------------

' The median slope of the engine's own lines, in degrees. Positive is clockwise.
'
' REPORTED, AND `grid` REFUSES PAST A THRESHOLD, because beyond about 3 degrees
' the reconstruction does not break -- it MISATTRIBUTES, putting one row's
' figure against another row's identity at the SAME confidence as a clean page.
' Confidence cannot see it: every word was read correctly and what went wrong is
' the association between them.
function skew_of(words)
    if count(words) = 0 then
        return unknown
    end if
    lines = {}
    for each d in words
        k = string(d.block) + "/" + string(d.paragraph) + "/" + string(d.line)
        if not has(lines, k) then
            lines[k] = []
        end if
        lines[k] = append(lines[k], d)
    end for
    slopes = []
    for each k in keys(lines)
        v = lines[k]
        ' THREE WORDS, MEASURED RATHER THAN CHOSEN. Four was the first value
        ' here, taken from an exploration over full-page photographs whose text
        ' lines are long -- and on a page tesseract has split into BLOCKS no
        ' line reaches four, so both fixtures answered `unknown` and the skew
        ' was silently unmeasurable exactly where it matters. Measured over the
        ' same two pages: min 4 finds 0 lines, min 3 finds 4 and reports
        ' +0.07 degrees, min 2 finds 7 and reports +0.00. Two is rejected not
        ' because it is wrong but because two points fit a line EXACTLY, so
        ' every such line contributes an unaveraged slope; three is the smallest
        ' count at which the fit means anything.
        if count(v) >= 3 then
            sx = 0
            sy = 0
            for each d in v
                sx = sx + d.left + d.width / 2
                sy = sy + d.top + d.height / 2
            end for
            mx = sx / count(v)
            my = sy / count(v)
            num = 0
            den = 0
            for each d in v
                cx = d.left + d.width / 2
                cy = d.top + d.height / 2
                num = num + (cx - mx) * (cy - my)
                den = den + (cx - mx) * (cx - mx)
            end for
            if den > 0 then
                append(slopes, num / den)
            end if
        end if
    end for
    if count(slopes) = 0 then
        return unknown
    end if
    return _degrees(_median(slopes))
end function

function _median(xs)
    s = sort(xs)
    n = count(s)
    if n = 0 then
        return unknown
    end if
    mid = floor(n / 2)
    if n / 2 = mid then
        return (s[mid - 1] + s[mid]) / 2
    end if
    return s[mid]
end function

' gBASIC has no trig builtins (chart.bas carries its own sine for the same
' reason), and a slope this small needs only the leading terms: atan(x) is
' x - x^3/3 + x^5/5 within a thousandth of a degree over the range a page skews.
' Beyond |x| = 1 the series diverges, and `grid` has refused long before that.
function _degrees(slope)
    x = slope
    if x > 1 then
        return 90
    end if
    if x < -1 then
        return -90
    end if
    a = x - (x * x * x) / 3 + (x * x * x * x * x) / 5 - (x * x * x * x * x * x * x) / 7
    return a * 180 / 3.14159265358979
end function

' ---------------------------------------------------------------------------
' The grid (design R3)
' ---------------------------------------------------------------------------

' MAX_SKEW is where the reconstruction stops being safe, and the number is
' stated rather than hidden because it was measured on ONE synthetic page in one
' font: rows survive to about 3 degrees and at 5 they MISATTRIBUTE. Real
' hand-held photographs came in under 2, so this refuses rarely -- but when it
' refuses it is refusing the case where the output would be a well-formed row
' carrying another row's figures, at the same confidence as a clean page.
function max_skew()
    return 3
end function

' `ocr.grid(page)` -> the page laid out as text, one line per row.
'
' THIS IS THE SEAM. What comes out is a print-image report, which is what `ari`
' parses and `ari_discover` infers a specification from -- so OCR is the front
' door to machinery that already exists rather than a new pipeline, and this
' library's job ends at producing a faithful layout.
'
' GROUPED BY THE ENGINE'S OWN LINES, not by binning the `top` coordinate. The
' engine has already done baseline fitting; binning re-does it worse, and
' measured, engine lines tolerate three times the skew. Lines whose vertical
' spans OVERLAP are then merged, which is what repairs the defect this whole
' design exists for: tesseract puts a right-hand column in a SEPARATE BLOCK, so
' an account number and its amount arrive twelve lines apart with the
' association destroyed and the text still reading like a document.
function grid(page)
    if not has(page, "words") then
        error "ocr.grid: expects a page from ocr.read_page"
    end if
    if count(page.words) = 0 then
        return ""
    end if
    ' REFUSED RATHER THAN LAID OUT SIDEWAYS OR CROOKED. A caller wanting the
    ' words anyway still has them -- `read_page` is unaffected, and the words
    ' are individually fine. What may not be claimed is that a ROW belongs
    ' together, when the evidence for that has gone.
    if not is_unknown(page.skew) then
        if page.skew > max_skew() or page.skew < 0 - max_skew() then
            error ("ocr.grid: the page is skewed " + string(page.skew)
                   + " degrees, past the " + string(max_skew())
                   + " where rows stop being reliable -- beyond it a row carries"
                   + " another row's values and nothing says so")
        end if
    end if

    ' one bucket per engine line
    lines = {}
    order = []
    for each d in page.words
        k = string(d.block) + "/" + string(d.paragraph) + "/" + string(d.line)
        if not has(lines, k) then
            lines[k] = []
            append(order, k)
        end if
        lines[k] = append(lines[k], d)
    end for

    rows = []
    for each k in order
        v = lines[k]
        sy = 0
        sh = 0
        for each d in v
            sy = sy + d.top + d.height / 2
            sh = sh + d.height
        end for
        append(rows, { mid: sy / count(v), h: sh / count(v), words: v })
    end for
    rows = _by_mid(rows)

    merged = []
    for each r in rows
        n = count(merged)
        if n > 0 then
            last = merged[n - 1]
            gap = r.mid - last.mid
            if gap < 0 then
                gap = 0 - gap
            end if
            if gap < last.h * 0.6 then
                last.words = _concat(last.words, r.words)
                last.mid = (last.mid + r.mid) / 2
                merged[n - 1] = last
            else
                append(merged, r)
            end if
        else
            append(merged, r)
        end if
    end for

    ' one character cell, from the median width per character across the page
    widths = []
    for each d in page.words
        n = len(d.text)
        if n > 0 then
            append(widths, d.width / n)
        end if
    end for
    cw = _median(widths)
    if is_unknown(cw) or cw <= 0 then
        cw = 1
    end if

    out = []
    for each r in merged
        line = ""
        for each d in _by_left(r.words)
            col = round(d.left / cw)
            while len(line) < col
                line = line + " "
            end while
            line = line + d.text
        end for
        append(out, line)
    end for
    return join(out, chr(10))
end function

function _concat(a, b)
    out = a
    for each x in b
        append(out, x)
    end for
    return out
end function

function _by_mid(rows)
    out = rows
    i = 1
    while i < count(out)
        j = i
        while j > 0 and out[j - 1].mid > out[j].mid
            t = out[j - 1]
            out[j - 1] = out[j]
            out[j] = t
            j = j - 1
        end while
        i = i + 1
    end while
    return out
end function

function _by_left(ws)
    out = ws
    i = 1
    while i < count(out)
        j = i
        while j > 0 and out[j - 1].left > out[j].left
            t = out[j - 1]
            out[j - 1] = out[j]
            out[j] = t
            j = j - 1
        end while
        i = i + 1
    end while
    return out
end function

end library
