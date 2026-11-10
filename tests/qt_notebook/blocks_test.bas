' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' Blocks between a source's lines, folds, and the `block` event (Qt design
' §0.38 A, B2, B3), as a program sees them. Every line number here is read back
' from Qt (qt.blocks, qt.fold), never from what was sent. What gBASIC cannot
' see -- the undo stack, the modified flag, copying across a block, a click
' below a gap -- is tests/qt/blocks_probe.cpp's, linking the real backend.
' args[0] is a scratch directory for pictures the runner decodes.

window nb( title: "Notebook", width: 700, height: 1500 )
    column
        source ed( language: "gBASIC" )
        source dark( language: "gBASIC", theme: "dark" )
        memo notes()
        markdown preview()
    end column

    ' a click on a block, its collapse control, or a link in a markdown block
    on ed.block
        append(g.events, qt.block_event())
    end on

    ' a link pressed in the markdown part: never followed, only reported
    on preview.link
        append(g.links, qt.link())
    end on
end window

function lines_of(n)
    out = []
    for i = 1 to n
        append(out, "x" + string(i) + " = " + string(i))
    next
    return join(out, "\n")
end function

' The line each block is on now, as Qt holds it, as "id@line".
function where(part)
    out = []
    for each b in qt.blocks(nb, part)
        append(out, b.id + "@" + string(b.line))
    next
    return out
end function

function one(part, id)
    for each b in qt.blocks(nb, part)
        if b.id = id then
            return b
        end if
    next
    return unknown
end function

program main(args)
    g = { checks: 0, bad: 0, events: [], links: [] }
    if not qt.can_display() then
        print "NODISPLAY"
        return
    end if
    dir = args[0]
    qt.open(nb)
    qt.write(nb, "ed", lines_of(40))

    print "-- kinds, and what Qt holds"
    qt.blocks(nb, "ed", [
        { id: "out", line: 3, kind: "text", content: "3 of 41 tapes rejected\n  indented stays" },
        { id: "doc", line: 6, kind: "markdown", content: "**Rates** are read from [the feed](https://example.com/feed)." },
        { id: "tbl", line: 9, kind: "table", content: { columns: ["date", "rate"], rows: [[{date}"2027-01-02", 4.1], [{date}"2027-01-03", 4.25]] } },
        { id: "fig", line: 12, kind: "image", content: { bytes: "<svg xmlns='http://www.w3.org/2000/svg' width='120' height='40'><rect width='120' height='40' fill='#3366cc'/></svg>", format: "svg" } }
    ])
    check("every block, in order, on its line", where("ed"), ["out@3", "doc@6", "tbl@9", "fig@12"])
    b = one("ed", "out")
    check("a block's kind is read back", b.kind, "text")
    check("and it has a height", b.height > 20, true)
    check("and it is on screen", b.visible, true)
    check("not collapsed, not covering", [b.collapsed, b.covers], [false, false])
    check("an image keeps its aspect inside its block", one("ed", "fig").height < 120, true)
    qt.caret_to(nb, "ed", 13, 1)
    check("with the caret below it, the image block is on screen", one("ed", "fig").visible, true)
    shot = qt.capture(nb, join_path(dir, "kinds.png"), { part: "ed" })
    qt.caret_to(nb, "ed", 1, 1)
    check("a capture of the editor includes them (pixels: the runner)", shot.width > 0, true)

    print ""
    print "-- anchored through edits"
    qt.replace_text(nb, "ed", { line: 1, column: 1, line2: 1, column2: 1, text: "' a new first line\n" })
    check("a line typed above moves every block with its line", where("ed"), ["out@4", "doc@7", "tbl@10", "fig@13"])
    qt.replace_text(nb, "ed", { line: 20, column: 1, line2: 20, column2: 1, text: "below\n" })
    check("a line typed below moves none", where("ed"), ["out@4", "doc@7", "tbl@10", "fig@13"])
    ' split line 4 (out's) in two: Enter in its middle. The block follows the
    ' line's END, so it stays under the second half, below the code it ran.
    qt.replace_text(nb, "ed", { line: 4, column: 3, line2: 4, column2: 3, text: "\n" })
    check("a line split in two keeps its block under the second half", where("ed"), ["out@5", "doc@8", "tbl@11", "fig@14"])
    qt.replace_text(nb, "ed", { line: 4, column: 3, line2: 5, column2: 1, text: "" })
    check("and joined again, back where it was", where("ed"), ["out@4", "doc@7", "tbl@10", "fig@13"])
    ' join line 7 (doc's) onto line 6: Backspace at its start
    qt.replace_text(nb, "ed", { line: 6, column: 7, line2: 7, column2: 1, text: "" })
    check("a line joined onto the one above takes its block with it", where("ed"), ["out@4", "doc@6", "tbl@9", "fig@12"])
    ' delete line 9 (tbl's) whole, with its line break
    qt.replace_text(nb, "ed", { line: 9, column: 1, line2: 10, column2: 1, text: "" })
    check("a deleted line drops its block", where("ed"), ["out@4", "doc@6", "fig@11"])
    ' empty line 11 (fig's) but keep it
    line11 = split(qt.read(nb, "ed", "plainText"), "\n")[10]
    qt.replace_text(nb, "ed", { line: 11, column: 1, line2: 11, column2: len(line11) + 1, text: "" })
    check("an emptied line drops its block too", where("ed"), ["out@4", "doc@6"])
    qt.write(nb, "ed", lines_of(40))
    check("a text replaced whole drops them all", where("ed"), [])

    print ""
    print "-- replace-all, reconciled by id"
    qt.blocks(nb, "ed", [{ id: "a", line: 2, kind: "text", content: "A" }, { id: "b", line: 4, kind: "text", content: "B" }])
    qt.blocks(nb, "ed", [{ id: "b", line: 30, kind: "text", content: "B" }, { id: "c", line: 1, kind: "text", content: "C" }])
    check("a replace is the whole set, at the lines it gives", where("ed"), ["b@30", "c@1"])
    qt.blocks(nb, "ed", [])
    check("an empty list clears them", qt.blocks(nb, "ed"), [])

    print ""
    print "-- collapsed, covers, and the line a block is on"
    qt.blocks(nb, "ed", [
        { id: "open", line: 2, kind: "text", content: "one\ntwo\nthree\nfour" },
        { id: "shut", line: 5, kind: "text", content: "one\ntwo\nthree\nfour", collapsed: true },
        { id: "over", line: 8, kind: "markdown", content: "prose *in place* of a comment", covers: true },
        { id: "tall", line: 10, kind: "text", content: lines_of(200), max_height: 100 },
        { id: "far", line: 39, kind: "text", content: "far down" }
    ])
    check("collapsed shows only its header", one("ed", "shut").height < one("ed", "open").height, true)
    check("and says so", one("ed", "shut").collapsed, true)
    check("a covering block says so", one("ed", "over").covers, true)
    check("max_height caps a block", one("ed", "tall").height <= 100, true)
    check("a block below the view is not visible", one("ed", "far").visible, false)
    qt.caret_to(nb, "ed", 40, 1)
    ' The scrollbar's range must include the blocks' heights, or the end of
    ' the text is unreachable: measured, without that this caret_to scrolled
    ' six lines and left line 40 below the view.
    check("until the editor scrolls to it", one("ed", "far").visible, true)
    qt.caret_to(nb, "ed", 1, 1)

    print ""
    print "-- folds"
    qt.fold(nb, "ed", [{ from: 12, to: 16 }, { from: 20, to: 22 }])
    check("the folds Qt holds", qt.fold(nb, "ed"), [{ from: 12, to: 16 }, { from: 20, to: 22 }])
    qt.replace_text(nb, "ed", { line: 1, column: 1, line2: 1, column2: 1, text: "top\n" })
    check("a fold moves with its lines", qt.fold(nb, "ed"), [{ from: 13, to: 17 }, { from: 21, to: 23 }])
    check("plainText still holds the folded lines", count(split(qt.read(nb, "ed", "plainText"), "\n")), 41)
    qt.caret_to(nb, "ed", 15, 1)
    check("a caret placed inside a fold is where it was put (Studio unfolds)", qt.caret(nb, "ed").line, 15)
    qt.fold(nb, "ed", [])
    qt.caret_to(nb, "ed", 15, 1)
    qt.fold(nb, "ed", [{ from: 13, to: 17 }])
    check("a fold that hides the caret's line moves it to the fold's first line", qt.caret(nb, "ed").line, 13)
    qt.fold(nb, "ed", [])
    check("an empty list unfolds", qt.fold(nb, "ed"), [])

    print ""
    print "-- the block event"
    qt.blocks(nb, "ed", [
        { id: "doc", line: 2, kind: "markdown", content: "see [the feed](https://example.com/feed) now" },
        { id: "tbl", line: 4, kind: "table", content: { columns: ["n"], rows: [[1]] } }
    ])
    qt.block_press(nb, "ed", "tbl", "click")
    qt.block_press(nb, "ed", "doc", "collapse")
    qt.block_press(nb, "ed", "doc", "link", "https://example.com/feed")
    check("a click, a collapse and a link, each with its block's id", g.events,
          [{ id: "tbl", action: "click", target: unknown },
           { id: "doc", action: "collapse", target: unknown },
           { id: "doc", action: "link", target: "https://example.com/feed" }])
    check("a collapse control is Studio's to act on: the block stays open", one("ed", "doc").collapsed, false)
    check("outside a handler there is no event", is_unknown(qt.block_event()), true)
    qt.blocks(nb, "dark", [{ id: "d", line: 1, kind: "text", content: "dark output" }])
    qt.block_press(nb, "dark", "d", "click")
    check("a source with no block handler fires none", count(g.events), 3)

    print ""
    print "-- theme"
    qt.write(nb, "dark", lines_of(5))
    qt.blocks(nb, "dark", [{ id: "d", line: 2, kind: "text", content: "dark output\nsecond" }])
    shot = qt.capture(nb, join_path(dir, "dark.png"), { part: "dark" })
    check("the dark editor is captured with its block (pixels: the runner)", shot.width > 0, true)
    d = one("dark", "d")
    print "darkblock " + string(d.height)

    print ""
    print "-- the markdown part (B4)"
    qt.set(nb, "preview", "markdown", "# Rates\n\nRead from [the feed](https://example.com/feed) and [notes](notes.md).\n\n| a | b |\n|---|---|\n| 1 | 2 |\n")
    shown = qt.read(nb, "preview", "plainText")
    check("markdown is rendered, not shown as markup", contains(shown, "Read from the feed and notes.") and not contains(shown, "]("), true)
    check("a GitHub table is a table", contains(shown, "a") and not contains(shown, "|---|"), true)
    qt.link_press(nb, "preview", "notes.md")
    qt.link_press(nb, "preview", "https://example.com/feed")
    check("each link pressed is reported, with its target", g.links, ["notes.md", "https://example.com/feed"])
    check("and none is followed: the part still shows the same prose", qt.read(nb, "preview", "plainText"), shown)
    check("outside a handler there is no link", is_unknown(qt.link()), true)

    print ""
    print "-- refused by name"
    on error goto next
    qt.blocks(nb, "ed", [{ id: "x", line: 1, kind: "text", content: "a", colapsed: true }])
    check("an unknown field, with the near miss", contains(error.message, "has a field `colapsed` -- did you mean `collapsed`?"), true)
    error.clear()
    qt.blocks(nb, "ed", [{ id: "x", line: 1, kind: "txt", content: "a" }])
    check("an unknown kind, with the near miss", contains(error.message, "`txt` as its kind -- did you mean `text`?"), true)
    error.clear()
    qt.blocks(nb, "ed", [{ id: "x", line: 1, kind: "text", content: "a" }, { id: "x", line: 2, kind: "text", content: "b" }])
    check("two blocks with one id", contains(error.message, "two blocks are called `x`"), true)
    error.clear()
    qt.blocks(nb, "ed", [{ id: "x", line: 99, kind: "text", content: "a" }])
    check("a line past the end, naming how many there are", contains(error.message, "block `x` is on line 99, and `ed` has 41 lines"), true)
    error.clear()
    qt.blocks(nb, "ed", [{ id: "x", line: 0, kind: "text", content: "a" }])
    check("line 0", contains(error.message, "line 1 is the first line"), true)
    error.clear()
    qt.blocks(nb, "ed", [{ id: "x", line: 1, kind: "table", content: { columns: ["a", "b"], rows: [[1]] } }])
    check("a table row short a cell", contains(error.message, "row 1 does not have 2 cells"), true)
    error.clear()
    qt.blocks(nb, "ed", [{ id: "x", line: 1, kind: "image", content: join_path(dir, "no-such.png") }])
    check("an image file that is not there", contains(error.message, "could not be read"), true)
    error.clear()
    qt.blocks(nb, "ed", [{ id: "x", line: 1, kind: "image", content: { bytes: "not an image", format: "png" } }])
    check("bytes that are no image", contains(error.message, "not an image Qt can read"), true)
    error.clear()
    qt.blocks(nb, "ed", [{ id: "x", line: 1, kind: "text", content: "a", max_height: 5000 }])
    check("a max_height out of range", contains(error.message, "from 20 to 2000"), true)
    error.clear()
    qt.blocks(nb, "notes", [])
    check("a memo has no blocks", contains(error.message, "notes"), true)
    error.clear()
    qt.fold(nb, "ed", [{ from: 5, to: 3 }])
    check("a fold that ends before it begins", contains(error.message, "the fold 5 to 3 is not a range"), true)
    error.clear()
    qt.block_press(nb, "ed", "nope", "click")
    check("pressing a block that is not there", contains(error.message, "has no block `nope`"), true)
    error.clear()
    qt.link_press(nb, "preview", "https://nowhere.example")
    check("pressing a link that is not there", contains(error.message, "has no link to `https://nowhere.example`"), true)
    error.clear()
    qt.link_press(nb, "notes", "x")
    check("a memo is not a markdown part", contains(error.message, "`notes` is not a `markdown` part"), true)
    error.clear()
    on error stop
    check("and a refused set left the last good set", where("ed"), ["doc@2", "tbl@4"])

    qt.close(nb)
    print ""
    print "checks: " + string(g.checks)
    print "mismatches: " + string(g.bad)
end program

function check(what, got, want)
    g.checks = g.checks + 1
    if string(got) = string(want) then
        print "ok   " + what
    else
        g.bad = g.bad + 1
        print "MISMATCH " + what + ": got " + string(got) + ", want " + string(want)
    end if
    return nothing
end function
