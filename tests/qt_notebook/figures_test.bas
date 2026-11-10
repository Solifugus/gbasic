' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' qt.render_svg and named syntax themes (Qt design §0.38 C and D1). args[0] is
' a scratch directory; the runner put a theme called "Gray Print" on the
' syntax path (GBASIC_SYNTAX_PATH/themes/gray.theme). Sizes are read from each
' PNG's own header bytes; pixels are the runner's PIL tier. Each refusal is
' cleared where it was caught: `error` is the frame's own, so a helper
' function clearing it would clear nothing of its caller's.

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

' width and height from a PNG's IHDR, which starts at byte 16
function png_size(path)
    f {file}= path
    b = read(f)
    return [unpack(">I", byte_slice(b, 16, 4))[0], unpack(">I", byte_slice(b, 20, 4))[0]]
end function

program main(args)
    G = { checks: 0, mismatches: 0 }
    dir = args[0]
    ' a 200x100 drawing: a blue box at 10,10 80x60, a black line, a word
    svg = ("<svg xmlns='http://www.w3.org/2000/svg' width='200' height='100' viewBox='0 0 200 100'>"
           + "<rect x='10' y='10' width='80' height='60' fill='#3366cc'/>"
           + "<path d='M100 90 L190 20' stroke='black' stroke-width='2'/>"
           + "<text x='100' y='40' font-family='sans-serif' font-size='14'>Łódź</text></svg>")

    print "-- render_svg"
    r = qt.render_svg(svg, join_path(dir, "own.png"))
    check("its own size, from the SVG", [r.width, r.height], [200, 100])
    check("the file's header agrees", png_size(join_path(dir, "own.png")), [200, 100])
    check("the answer names the file", file_name(string(r.file)), "own.png")
    r = qt.render_svg(svg, join_path(dir, "x2.png"), { scale: 2 })
    check("scale 2 doubles both", png_size(join_path(dir, "x2.png")), [400, 200])
    r = qt.render_svg(svg, join_path(dir, "w.png"), { width: 500 })
    check("a width alone keeps the aspect ratio", [r.width, r.height], [500, 250])
    r = qt.render_svg(svg, join_path(dir, "h.png"), { height: 50 })
    check("a height alone too", [r.width, r.height], [100, 50])
    r = qt.render_svg(svg, join_path(dir, "both.png"), { width: 300, height: 300 })
    check("both given are both used", png_size(join_path(dir, "both.png")), [300, 300])
    f {file}= join_path(dir, "file.png")
    r = qt.render_svg(svg, f)
    check("a {file} as the target", r.width, 200)
    r = qt.render_svg(svg, join_path(dir, "again.png"))
    a {file}= join_path(dir, "own.png")
    b {file}= join_path(dir, "again.png")
    check("drawn twice, the same bytes", hex_encode(sha256(read(a))) = hex_encode(sha256(read(b))), true)

    print ""
    print "-- refused by name"
    on error goto next
    x = qt.render_svg(42, join_path(dir, "n.png"))
    check("not text", contains(default(error.message, ""), "expects the SVG as text"), true)
    error.clear()
    x = qt.render_svg("<p>not svg</p>", join_path(dir, "n.png"))
    check("text that is no SVG", contains(default(error.message, ""), "not an SVG that QtSvg can draw"), true)
    error.clear()
    x = qt.render_svg(svg, join_path(dir, "n.png"), { scal: 2 })
    check("a misspelt option", contains(default(error.message, ""), "does not know the option `scal` -- did you mean `scale`?"), true)
    error.clear()
    x = qt.render_svg(svg, join_path(dir, "n.png"), { width: 0 })
    check("a width of 0", contains(default(error.message, ""), "`width` as a whole number of pixels from 1 to 20000"), true)
    error.clear()
    x = qt.render_svg(svg, join_path(dir, "n.png"), { width: 10.5 })
    check("a fractional width", contains(default(error.message, ""), "`width` as a whole number"), true)
    error.clear()
    x = qt.render_svg(svg, join_path(dir, "n.png"), { scale: 9 })
    check("a scale past 8", contains(default(error.message, ""), "`scale` as a number above 0 and at most 8"), true)
    error.clear()
    x = qt.render_svg(svg, join_path(dir, "no/such/folder.png"))
    check("a folder that is not there fails as write does", contains(default(error.message, ""), "folder.png"), true)
    error.clear()
    x = qt.render_svg(svg, join_path(dir, "n.png"), [2])
    check("options that are not a record", contains(default(error.message, ""), "expects its options as a record"), true)
    error.clear()
    on error stop
    n {file}= join_path(dir, "n.png")
    check("and no refusal wrote a file", exists(n), false)

    print ""
    print "-- named themes"
    win = { kind: "window", name: "themes", options: { title: "Themes", width: 600, height: 300 },
            items: [ { kind: "container", word: "row", options: {}, items: [
                { kind: "part", word: "source", name: "gray", options: { language: "gBASIC", theme: "Gray Print" } },
                { kind: "part", word: "source", name: "printing", options: { language: "gBASIC", theme: "Printing" } },
                { kind: "part", word: "source", name: "dark", options: { language: "gBASIC", theme: "dark" } },
                { kind: "part", word: "source", name: "plain", options: { theme: "Gray Print" } } ] } ] }
    qt.open(win)
    qt.set(win, "gray", "plainText", "' a comment\nx = 42\n")
    check("a theme from the syntax path's themes/", qt.highlighting(win, "gray").theme, "Gray Print")
    check("one KSyntaxHighlighting ships", qt.highlighting(win, "printing").theme, "Printing")
    check("dark still names the theme it resolved to", qt.highlighting(win, "dark").theme, "Breeze Dark")
    check("a themed plain editor has no highlighter to ask", is_unknown(qt.highlighting(win, "plain")), true)
    shot = qt.capture(win, join_path(dir, "themes.png"), { part: "gray" })
    check("the themed editor is captured", shot.width > 0, true)
    shot = qt.capture(win, join_path(dir, "plain.png"), { part: "plain" })
    qt.close(win)

    print ""
    print "-- an unknown theme, refused"
    on error goto next
    bad = { kind: "window", name: "bad", options: { title: "B" },
            items: [ { kind: "part", word: "source", name: "s", options: { theme: "Gray Prnt" } } ] }
    qt.open(bad)
    check("a near miss is named", contains(default(error.message, ""), "is not a theme -- did you mean \"Gray Print\"?"), true)
    error.clear()
    bad.items[0].options.theme = "Nothing Like It"
    qt.open(bad)
    check("otherwise the names are listed", contains(default(error.message, ""), "a theme's name: \"Atom One Dark\""), true)
    error.clear()
    on error stop

    print ""
    print "checks: " + string(G.checks)
    print "mismatches: " + string(G.mismatches)
end program
