' STUDIO'S FIND-AND-REPLACE REQUESTS (docs/qt_gui_design.md §0.36): a part's
' caption, whitespace shown, a range selected, edits as one undo step, every
' match marked, and a left click. Under QT_QPA_PLATFORM=offscreen.
'
' THE ORACLE IS QT, never the call's own arguments: `qt.label` finds the QLabel
' whose buddy is the part, `qt.read(..., "whitespace")` reads the document's
' text option, `qt.selection` and `qt.highlights` read the cursor and the extra
' selections back, an undo is Qt's own `undo` slot, and a click is a mouse
' press Qt turns into `itemClicked` itself. Each claim sits beside a control.

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

function refusal(win, verb, part, a)
    on error goto handle
    if verb = "select_text" then
        r = qt.select_text(win, part, a[0], a[1], a[2], a[3])
    end if
    if verb = "replace_text" then
        r = qt.replace_text(win, part, a)
    end if
    if verb = "highlight_text" then
        r = qt.highlight_text(win, part, a)
    end if
    if verb = "whitespace" then
        r = qt.set(win, part, "whitespace", true)
    end if
    if verb = "click" then
        r = qt.click(win, part, a)
    end if
    return "ACCEPTED"
handle:
    m = error.message
    error.clear()
    return m
end function

window app( title: "Find", width: 640, height: 640 )
    column( margin: 8 )
        form to edit person
            field who( label: "Name" )
            choice licence( label: "Licence", options: ["Apache-2.0", "MIT"] )
            memo about( label: "About" )
            check agree( label: "I agree" )
            field bare()
        end form
        row
            choice pick( label: "Pick", options: ["a", "b"] )
            field near( label: "Near" )
        end row
        cells
            row
                choice cell_pick( label: "Cell", options: ["x"] )
            end row
        end cells
        memo log( label: "You are in the cellar." )
        source ed( language: "gBASIC" )
        memo notes()
        button go( text: "Go" )
        tree files to show rows
            field name( label: "File" )
        end tree
        table grid to show rows
            field name( label: "File" )
        end table
    end column

    on grid.cellClicked
        g.cell_clicked = g.cell_clicked + 1
        g.cell_row = qt.selected(app, "grid")
    end on

    on files.itemClicked
        g.item_clicked = g.item_clicked + 1
        g.clicked_row = qt.selected(app, "files")
    end on

    on go.clicked
        g.go = g.go + 1
    end on
end window

program main()
    if not qt.can_display() then
        print "NODISPLAY"
        return
    end if
    g = { checks: 0, bad: 0, item_clicked: 0, clicked_row: -1, go: 0, cell_clicked: 0, cell_row: -1 }
    person = { who: "", licence: "MIT", about: "", agree: false, bare: "" }
    rows = [ { name: "a.bas" }, { name: "b.bas" }, { name: "c.bas" } ]
    qt.open(app)

    print "-- CAPTIONS: the label beside a part, read from Qt"
    check("a field in a form has its row label", qt.label(app, "who"), "Name")
    check("a CHOICE in a form has its row label (Studio's licence)",
          qt.label(app, "licence"), "Licence")
    check("a memo in a form: the label is the row's caption", qt.label(app, "about"), "About")
    check("  and not the memo's text", qt.read(app, "about"), "")
    check("a check keeps its label as its own text, no caption",
          is_unknown(qt.label(app, "agree")), true)
    check("  (control: that text is there)", qt.read(app, "agree", "text"), "I agree")
    check("a field with no label has none", is_unknown(qt.label(app, "bare")), true)
    check("a choice in a row has its caption", qt.label(app, "pick"), "Pick")
    check("a field in a row has its caption", qt.label(app, "near"), "Near")
    check("a choice in cells has its caption", qt.label(app, "cell_pick"), "Cell")
    check("a memo outside a form keeps its label as text",
          qt.read(app, "log"), "You are in the cellar.")
    check("  with no caption", is_unknown(qt.label(app, "log")), true)

    print ""
    print "-- WHITESPACE: a setting read back from the document's text option"
    check("off by default", qt.read(app, "ed", "whitespace"), false)
    r = qt.set(app, "ed", "whitespace", true)
    check("on, on a source", qt.read(app, "ed", "whitespace"), true)
    check("  control: another editor is untouched", qt.read(app, "notes", "whitespace"), false)
    r = qt.set(app, "notes", "whitespace", true)
    check("on, on a memo", qt.read(app, "notes", "whitespace"), true)
    r = qt.set(app, "ed", "whitespace", false)
    check("and off again", qt.read(app, "ed", "whitespace"), false)
    check("refused on a field",
          refusal(app, "whitespace", "who", nothing),
          "qt.set: `whitespace` is a setting of a `source` or a `memo`, and `who` is neither")

    print ""
    print "-- SELECTING A RANGE"
    nl = chr(10)
    qt.write(app, "ed", "let x = 1" + nl + "print x" + nl + "x = x + 1")
    check("nothing is selected at first", is_unknown(qt.selection(app, "ed")), true)
    r = qt.select_text(app, "ed", 2, 7, 2, 8)
    s = qt.selection(app, "ed")
    check("the selected text is Qt's", s.text, "x")
    check("  and the range reads back", [s.line, s.column, s.line2, s.column2], [2, 7, 2, 8])
    c = qt.caret(app, "ed")
    check("the caret is at the range's end, selecting", [c.line, c.column, c.selecting], [2, 8, true])
    r = qt.select_text(app, "ed", 1, 5, 2, 6)
    check("a range across lines reads with a line feed",
          qt.selection(app, "ed").text, "x = 1" + nl + "print")
    r = qt.select_text(app, "ed", 1, 1, 1, 999)
    check("a column past its line clamps to the line's end",
          qt.selection(app, "ed").text, "let x = 1")
    check("a line past the end is refused, with the real length",
          refusal(app, "select_text", "ed", [9, 1, 9, 2]),
          "qt.select_text: that line is past the end -- this editor holds 3")
    check("a range ending before it starts is refused",
          refusal(app, "select_text", "ed", [2, 3, 1, 1]),
          "qt.select_text: a range ends before it starts -- give the earlier position first")
    check("refused on a button",
          refusal(app, "select_text", "go", [1, 1, 1, 2]),
          "qt.select_text: only a `source` or a `memo` holds text to select, and `go` is neither")

    print ""
    print "-- COLUMNS COUNT CODEPOINTS, as len and regex do"
    star = chr(127775)
    qt.write(app, "notes", star + "ab")
    r = qt.select_text(app, "notes", 1, 2, 1, 3)
    check("column 2 is after the emoji (Qt counts it as two)", qt.selection(app, "notes").text, "a")
    r = qt.caret_to(app, "notes", 1, 3)
    check("and the caret agrees", qt.caret(app, "notes").column, 3)

    print ""
    print "-- REPLACING AS AN EDIT: one undo step"
    original = "let x = 1" + nl + "print x" + nl + "x = x + 1"
    qt.write(app, "ed", original)
    r = qt.replace_text(app, "ed", { line: 2, column: 7, line2: 2, column2: 8, text: "total" })
    check("one edit", qt.read(app, "ed"), "let x = 1" + nl + "print total" + nl + "x = x + 1")
    u = qt.invoke(app, "ed", "undo")
    check("  and Qt's undo restores it", qt.read(app, "ed"), original)
    all = [ { line: 1, column: 5, line2: 1, column2: 6, text: "count" },
            { line: 2, column: 7, line2: 2, column2: 8, text: "count" },
            { line: 3, column: 1, line2: 3, column2: 2, text: "count" },
            { line: 3, column: 5, line2: 3, column2: 6, text: "count" } ]
    r = qt.replace_text(app, "ed", all)
    replaced = "let count = 1" + nl + "print count" + nl + "count = count + 1"
    check("four edits, every position in the text BEFORE the call", qt.read(app, "ed"), replaced)
    u = qt.invoke(app, "ed", "undo")
    check("  and ONE undo restores all four", qt.read(app, "ed"), original)
    u = qt.invoke(app, "ed", "redo")
    check("  and one redo puts them back", qt.read(app, "ed"), replaced)
    qt.write(app, "ed", original)
    r = qt.replace_text(app, "ed", [ { line: 2, column: 1, line2: 2, column2: 1, text: "' " } ])
    check("an empty range inserts", qt.read(app, "ed"), "let x = 1" + nl + "' print x" + nl + "x = x + 1")
    qt.write(app, "ed", original)
    check("overlapping edits are refused, and nothing changes",
          refusal(app, "replace_text", "ed",
                  [ { line: 1, column: 1, line2: 1, column2: 4, text: "a" },
                    { line: 1, column: 3, line2: 1, column2: 6, text: "b" } ]),
          "qt.replace_text: edits 1 and 2 overlap, so the result would depend on their order -- nothing was changed")
    check("  (the text is as it was)", qt.read(app, "ed"), original)
    check("a bad line in edit 2 refuses all of them",
          refusal(app, "replace_text", "ed",
                  [ { line: 1, column: 1, line2: 1, column2: 2, text: "L" },
                    { line: 7, column: 1, line2: 7, column2: 2, text: "Z" } ]),
          "qt.replace_text: edit 2: that line is past the end -- this editor holds 3")
    check("  (and edit 1 did not happen)", qt.read(app, "ed"), original)
    check("an edit with no text is refused",
          refusal(app, "replace_text", "ed", { line: 1, column: 1, line2: 1, column2: 2 }),
          "qt.replace_text: item 1 has no `text` to put in the range")

    print ""
    print "-- MARKING EVERY MATCH: Qt's extra selections, one list per call"
    check("none at first", qt.highlights(app, "ed"), [])
    found = [ { line: 1, column: 5, line2: 1, column2: 6 },
              { line: 3, column: 1, line2: 3, column2: 2 } ]
    r = qt.highlight_text(app, "ed", found)
    check("two marked, read back from Qt", qt.highlights(app, "ed"), found)
    check("  control: the text is unchanged", qt.read(app, "ed"), original)
    check("  and the selection is untouched", is_unknown(qt.selection(app, "ed")), true)
    r = qt.highlight_text(app, "ed", [ { line: 2, column: 7, line2: 2, column2: 8 } ])
    check("a second call replaces the list", qt.highlights(app, "ed"),
          [ { line: 2, column: 7, line2: 2, column2: 8 } ])
    r = qt.highlight_text(app, "ed", [])
    check("[] clears it", qt.highlights(app, "ed"), [])
    check("a bad range refuses the whole list",
          refusal(app, "highlight_text", "ed", [ { line: 5, column: 1, line2: 5, column2: 2 } ]),
          "qt.highlight_text: range 1: that line is past the end -- this editor holds 3")

    print ""
    print "-- A LEFT CLICK: a mouse press Qt turns into itemClicked"
    check("nothing clicked yet", g.item_clicked, 0)
    r = qt.click(app, "files", 2)
    check("clicking row 2 fired itemClicked", g.item_clicked, 1)
    check("  about row 2", g.clicked_row, 2)
    r = qt.click(app, "files", 0)
    check("clicking row 0 fired it again, about row 0", [g.item_clicked, g.clicked_row], [2, 0])
    r = qt.click(app, "grid", 1)
    check("clicking a TABLE row fired cellClicked, about row 1", [g.cell_clicked, g.cell_row], [1, 1])
    r = qt.click(app, "go")
    check("clicking a button fires clicked", g.go, 1)
    r = qt.right_click(app, "files", 1)
    check("control: a right-click does not fire itemClicked", g.item_clicked, 2)
    check("a row past the end is refused",
          refusal(app, "click", "files", 9), "qt.click: that tree has no such row")

    qt.close(app)
    print ""
    print "ok " + string(g.checks) + " checks, " + string(g.bad) + " mismatches"
end program
