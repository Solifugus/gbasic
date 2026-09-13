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
    on error stop
    check("CONTROL: a well-formed document still renders", byte_count(a) > 400, true)

    print ""
    print "checks: " + string(G.checks)
    print "mismatches: " + string(G.mismatches)
end program

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
