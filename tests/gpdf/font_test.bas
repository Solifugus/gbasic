' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' gpdf embedded fonts (book gap G58): reading a font file, measuring with it,
' and every refusal. args[0] is the directory tests/gpdf/make_font.py wrote.
'
' Widths here are computed by hand from make_font.py's advances (units per em
' 1000), not read back from the library. Whether the GLYPHS are right is the
' runner's oracle (check_font.py, fontTools against the original file).

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

' The six letters before "+name" in a rendered document.
function tag_of(pdf, name)
    at = byte_find(pdf, "+" + name)
    if is_nothing(at) then return ""
    return byte_slice(pdf, at - 6, 6)
end function

program main(args)
    load gpdf
    G = { checks: 0, mismatches: 0 }
    dir = args[0]
    f = gpdf.font_file(join_path(dir, "fixture.ttf"))

    print "-- reading the file"
    check("the PostScript name", f.name, "FixtureSans")
    check("units per em", f.upem, 1000)
    check("glyph count", f.glyphs, 32)
    check("a format 12 cmap is chosen over format 4", f.cmap.format, 12)
    check("fewer metrics than glyphs (the bare tail of hmtx)", f.metrics < f.glyphs, true)

    print ""
    print "-- measuring is the sum of the advances"
    check("'Za' at 10pt: (610 + 520) / 1000 * 10", gpdf.text_width(f, 10, "Za"), 11.3)
    check("'Łódź' at 12pt: (600 + 555 + 560 + 470) / 1000 * 12", gpdf.text_width(f, 12, "Łódź"), 26.22)
    check("past the BMP: U+1D400 at 10pt is 7", gpdf.text_width(f, 10, "𝐀"), 7)
    check("the hmtx tail: '123' at 10pt is 3 * 3.33", gpdf.text_width(f, 10, "123"), 9.99)
    check("the empty string is zero wide", gpdf.text_width(f, 10, ""), 0)
    lines = gpdf.wrap(f, 10, 60, "Zakład Mechaniczny Łódź")
    widest = 0
    for each l in lines
        if gpdf.text_width(f, 10, l) > widest then widest = gpdf.text_width(f, 10, l)
    end for
    check("wrap breaks a line the embedded font measures too wide", count(lines) > 1, true)
    check("and every line fits", widest <= 60, true)
    check("and nothing is lost", join(lines, " "), "Zakład Mechaniczny Łódź")

    print ""
    print "-- representable asks the font"
    check("Polish is representable in the font", gpdf.representable("Zakład Łódź", f), true)
    check("and not in WinAnsi", gpdf.representable("Zakład Łódź"), false)
    check("a letter the font lacks is not", gpdf.representable("Q", f), false)
    check("Arabic is not (it needs shaping)", gpdf.representable("سلام", f), false)
    check("a tab is not", gpdf.representable("a" + chr(9), f), false)

    print ""
    print "-- what is refused, by name"
    d = gpdf.document({ title: "t" })
    d = gpdf.add_page(d)
    d = gpdf.set_font(d, f, 12)
    on error goto next
    d = gpdf.text_at(d, 72, 700, "ZaQ")
    check("a character the font lacks", contains(error.message, "'Q' (U+0051) is not in the font FixtureSans"), true)
    check("saying where it is", contains(error.message, "character 3 of 3"), true)
    check("and that nothing is substituted", contains(error.message, "nothing is substituted"), true)
    error.clear()
    d = gpdf.text_at(d, 72, 700, "aשלום")
    check("Hebrew (right to left)", contains(error.message, "U+05E9") and contains(error.message, "shaping"), true)
    error.clear()
    d = gpdf.text_at(d, 72, 700, "سلام")
    check("Arabic (joined)", contains(error.message, "U+0633") and contains(error.message, "shaping"), true)
    error.clear()
    d = gpdf.text_at(d, 72, 700, "नमस्ते")
    check("Devanagari (reordered)", contains(error.message, "U+0928") and contains(error.message, "shaping"), true)
    error.clear()
    d = gpdf.text_at(d, 72, 700, "a" + chr(9) + "b")
    check("a control character", contains(error.message, "U+0009 is a control character"), true)
    error.clear()
    w = gpdf.text_width(f, 10, "Q")
    check("measuring refuses as drawing does", contains(error.message, "gpdf.text_width: 'Q'"), true)
    error.clear()
    w = gpdf.text_width("FixtureSans", 10, "Za")
    check("a name is not a font value", contains(error.message, "pass the value gpdf.font_file returned"), true)
    error.clear()
    e = gpdf.set_font(gpdf.document({}), "FixtureSans", 12)
    check("set_font of a name no document was given",
          contains(error.message, "'FixtureSans' is not a core-14 font") and contains(error.message, "gpdf.font_file(path)"), true)
    error.clear()
    x = gpdf.font_file(join_path(dir, "fixture.otf"))
    check("PostScript (CFF) outlines", contains(error.message, "has PostScript (CFF) outlines"), true)
    error.clear()
    x = gpdf.font_file(join_path(dir, "restricted.ttf"))
    check("a licence that forbids embedding", contains(error.message, "forbids embedding it (OS/2 fsType 2"), true)
    error.clear()
    x = gpdf.font_file(join_path(dir, "bitmaponly.ttf"))
    check("a licence for bitmaps only", contains(error.message, "embedding its bitmaps only"), true)
    error.clear()
    x = gpdf.font_file(join_path(dir, "helvetica.ttf"))
    check("a font calling itself Helvetica", contains(error.message, "a core-14 font's name"), true)
    error.clear()
    x = gpdf.font_file(join_path(dir, "fixture.ttf"), { index: 1 })
    check("an index for a single font", contains(error.message, "holds one font, not a collection"), true)
    error.clear()
    x = gpdf.font_file(join_path(dir, "pair.ttc"), { index: 2 })
    check("an index past a collection", contains(error.message, "a collection of 2 fonts"), true)
    error.clear()
    x = gpdf.font_file(join_path(dir, "fixture.ttf"), { size: 1 })
    check("an unknown option", contains(error.message, "unknown option 'size'"), true)
    error.clear()
    scratch = temp_dir()
    cut {file}= join_path(scratch, "cut.ttf")
    whole {file}= join_path(dir, "fixture.ttf")
    write(cut, byte_slice(read(whole), 0, 100))
    x = gpdf.font_file(join_path(scratch, "cut.ttf"))
    check("a file cut short", contains(error.message, "is cut short"), true)
    error.clear()
    notfont {file}= join_path(scratch, "notes.ttf")
    write(notfont, "these are not the glyphs you are looking for")
    x = gpdf.font_file(join_path(scratch, "notes.ttf"))
    check("a file that is no font", contains(error.message, "is not a TrueType or OpenType font"), true)
    error.clear()
    other = gpdf.font_file(join_path(dir, "two.ttf"), { name: "FixtureSans" })
    e = gpdf.set_font(d, other, 12)
    check("a second, different font under a name already used", contains(error.message, "already has a font called 'FixtureSans'"), true)
    error.clear()
    on error stop
    remove_tree(scratch)

    print ""
    print "-- and what is allowed"
    renamed = gpdf.font_file(join_path(dir, "helvetica.ttf"), { name: "HelveticaLookalike" })
    check("a core name is fine under another name", renamed.name, "HelveticaLookalike")
    check("a collection's first font by default", gpdf.font_file(join_path(dir, "pair.ttc")).name, "FixtureSans")
    check("and its second by index", gpdf.font_file(join_path(dir, "pair.ttc"), { index: 1 }).name, "FixtureTwo")
    again = gpdf.set_font(d, f, 9)
    check("the same font given twice is one font", count(again.fonts_used), 1)
    check("and by its name once given", gpdf.set_font(again, "FixtureSans", 10).font, "FixtureSans")

    print ""
    print "-- the document"
    a = gpdf.document({ title: "Łódź", created: {datetime}"2026-11-10 09:00:00" })
    a = gpdf.add_page(a)
    a = gpdf.set_font(a, f, 12)
    a = gpdf.text_at(a, 72, 700, "Łódź")
    pdf = gpdf.render(a)
    check("rendering twice gives the same bytes", gpdf.render(a) = pdf, true)
    check("a Type0 font over a CIDFontType2", contains(pdf, "/Subtype /Type0") and contains(pdf, "/Subtype /CIDFontType2"), true)
    check("with its program and a ToUnicode map", contains(pdf, "/FontFile2") and contains(pdf, "/ToUnicode"), true)
    ' Uncompressed, so the content stream can be read: the four characters
    ' are CIDs 1 to 4 in order of first use, and a repeat reuses its CID.
    u = gpdf.document({ compress: false })
    u = gpdf.add_page(u)
    u = gpdf.set_font(u, f, 12)
    u = gpdf.text_at(u, 72, 700, "ŁódźŁ")
    check("drawn as CIDs in order of first use", contains(gpdf.render(u), "<00010002000300040001> Tj"), true)
    tag1 = tag_of(pdf, "FixtureSans")
    check("the subset tag is six capitals", len(tag1) = 6 and upper(tag1) = tag1, true)
    b = gpdf.document({ title: "Łódź", created: {datetime}"2026-11-10 09:00:00" })
    b = gpdf.add_page(b)
    b = gpdf.set_font(b, f, 12)
    b = gpdf.text_at(b, 72, 700, "Zakład")
    check("a different subset has a different tag", tag_of(gpdf.render(b), "FixtureSans") != tag1, true)
    check("a title WinAnsi cannot write goes as UTF-16", contains(pdf, "/Title <FEFF0141"), true)
    t = gpdf.document({ title: "Price €9" })
    t = gpdf.add_page(t)
    check("so does one with a byte PDFDocEncoding reads otherwise", contains(gpdf.render(t), "/Title <FEFF"), true)
    t = gpdf.document({ title: "Müller (draft)" })
    t = gpdf.add_page(t)
    check("Latin-1 stays a literal string", contains(gpdf.render(t), "/Title (M" + from_bytes([ 0xFC ]) + "ller \\(draft\\))"), true)
    plain = gpdf.document({ title: "plain" })
    plain = gpdf.add_page(plain)
    plain = gpdf.text_at(plain, 72, 700, "Hello")
    check("a core-font document has no embedded font", contains(gpdf.render(plain), "FontFile2"), false)

    print ""
    print "checks: " + string(G.checks)
    print "mismatches: " + string(G.mismatches)
end program
