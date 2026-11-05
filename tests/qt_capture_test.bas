' A WINDOW'S PIXELS (docs/qt_gui_design.md §0.37): `qt.capture` writes a
' window, or one named part of it, to a PNG file at a chosen scale. Under
' QT_QPA_PLATFORM=offscreen. The books' figures of Studio are the reason
' (gbasic-books, volume 3's gap ledger, S1).
'
' THE ORACLE IS THE FILE, never the record the call answers: the width and
' height are read from the PNG's own header bytes, the part's size from Qt
' (`qt.read(..., "width")`), and the pixels themselves are judged by the
' runner with Python's PIL, which shares no code with Qt's PNG writer.
'
' Writes its pictures into the folder named by $OUT so the runner can look at
' them after.

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

function refusal(file, opts)
    on error goto handle
    r = qt.capture(app, file, opts)
    return "ACCEPTED"
handle:
    m = error.message
    error.clear()
    return m
end function

' a PNG's width and height, from bytes 16-23 of its IHDR chunk, big-endian
function png_size(path)
    b = read(path)
    if byte_at(b, 0) != 137 or byte_slice(b, 1, 3) != "PNG" then
        return "not a PNG"
    end if
    w = 0
    h = 0
    for i = 16 to 19
        w = w * 256 + byte_at(b, i)
        h = h * 256 + byte_at(b, i + 4)
    end for
    return [ w, h ]
end function

window app( title: "Shot", width: 400, height: 300 )
    menu "File"
        item act_quit( label: "Quit" )
    end menu
    column( margin: 8 )
        text heading( text: "Results" )
        text note( text: "Note" )
        source ed( language: "gBASIC" )
        tabs book
            tab "One"
                memo first( plainText: "first page" )
            end tab
            tab "Two"
                memo second( plainText: "second page" )
            end tab
        end tabs
        button go( text: "Go" )
    end column
end window

program main()
    if not qt.can_display() then
        print "NODISPLAY"
        return
    end if
    g = { checks: 0, bad: 0, n: 0, steady: [] }
    out = env("OUT")

    ' THE STEADY-STATE TIER'S TIMER, at the top of `main` where a `watch` is
    ' allowed. Each tick captures the focused editor while Qt's loop is
    ' running, which is when its caret blinks.
    beat = timer.every(0.13)
    watch(timer.ticks)
        while count(timer.ticks) > 0
            ev = take_first(timer.ticks)
            g.n = g.n + 1
            p = join_path(out, "steady" + string(g.n) + ".png")
            qt.capture(app, p, { part: "ed" })
            append(g.steady, read(p))
            if g.n >= 12 then
                timer.cancel(beat)
                qt.stop()
            end if
        end while
    end watch

    qt.open(app)
    qt.set(app, "ed", "plainText", "print 1\nprint 2\n")
    qt.set(app, "heading", "styleSheet", "background: #ff0000")
    qt.set(app, "note", "styleSheet", "background: #0000ff")

    print "-- THE WHOLE WINDOW"
    whole = join_path(out, "whole.png")
    r = qt.capture(app, whole)
    check("it answers the file it wrote, as a {file}", type(r.file), "file")
    check("  naming that file", string(r.file), whole)
    check("the PNG's own header gives the size it answered", png_size(whole), [ r.width, r.height ])
    check("and that is the window's size in Qt", [ r.width, r.height ],
          [ qt.read(app, "app", "width"), qt.read(app, "app", "height") ])
    check("a {file} is taken as well as a string",
          qt.capture(app, {file}join_path(out, "whole_ref.png")).width, r.width)
    check("  and writes the same bytes", read({file}join_path(out, "whole_ref.png")), read(whole))
    again = join_path(out, "again.png")
    m = app.capture(again)
    check("win.capture(...) is qt.capture(win, ...)", read(again), read(whole))

    print "-- A PART, AT A SCALE"
    hw = qt.read(app, "heading", "width")
    hh = qt.read(app, "heading", "height")
    r = qt.capture(app, join_path(out, "heading.png"), { part: "heading" })
    check("a part is captured at its own size", png_size(join_path(out, "heading.png")), [ hw, hh ])
    r = qt.capture(app, join_path(out, "heading2.png"), { part: "heading", scale: 2 })
    check("scale 2 doubles both sides", png_size(join_path(out, "heading2.png")), [ hw * 2, hh * 2 ])
    check("  and answers them", [ r.width, r.height ], [ hw * 2, hh * 2 ])
    r = qt.capture(app, join_path(out, "heading15.png"), { part: "heading", scale: 1.5 })
    check("a fractional scale rounds to whole pixels", png_size(join_path(out, "heading15.png")),
          [ round(hw * 1.5), round(hh * 1.5) ])
    r = qt.capture(app, join_path(out, "note.png"), { part: "note" })
    check("a second part, the runner's control colour", png_size(join_path(out, "note.png")),
          [ qt.read(app, "note", "width"), qt.read(app, "note", "height") ])
    r = qt.capture(app, join_path(out, "go.png"), { part: "go" })
    check("a button", png_size(join_path(out, "go.png")),
          [ qt.read(app, "go", "width"), qt.read(app, "go", "height") ])
    r = qt.capture(app, join_path(out, "page_one.png"), { part: "first" })
    check("a part on the selected tab", r.width, qt.read(app, "first", "width"))

    print "-- THE SAME WINDOW TWICE IS THE SAME FILE, A CHANGED ONE IS NOT"
    a = join_path(out, "same_a.png")
    b = join_path(out, "same_b.png")
    qt.capture(app, a, { part: "ed" })
    qt.capture(app, b, { part: "ed" })
    check("two captures of an unchanged part are byte-identical", read(a) = read(b), true)
    qt.set(app, "ed", "plainText", "print 3\nprint 2\n")
    qt.capture(app, b, { part: "ed" })
    check("one character changed changes the picture", read(a) = read(b), false)
    qt.set(app, "ed", "plainText", "print 1\nprint 2\n")
    qt.capture(app, b, { part: "ed" })
    check("  and changing it back gives the first picture again", read(a) = read(b), true)
    qt.set(app, "heading", "text", "A much longer heading than before")
    qt.capture(app, join_path(out, "heading_text.png"), { part: "heading" })
    check("a change of text is drawn at once, with no turn of the loop",
          read(join_path(out, "heading_text.png")) = read(join_path(out, "heading.png")), false)
    qt.set(app, "heading", "text", "Results")
    ' A LAYOUT CHANGE IS A POSTED EVENT: hiding the note gives its room to the
    ' editor only when the layout runs. Measured, without the capture running
    ' it the editor came out at its old height and Qt answered that too.
    before = qt.capture(app, join_path(out, "ed_before.png"), { part: "ed" })
    qt.set(app, "note", "visible", false)
    after = qt.capture(app, join_path(out, "ed_after.png"), { part: "ed" })
    check("a part hidden just before is laid out before the picture: the editor grew",
          after.height > before.height, true)
    check("  to the height Qt now gives it", after.height, qt.read(app, "ed", "height"))
    qt.set(app, "note", "visible", true)

    print "-- REFUSALS"
    check("an unknown part names the parts there are", refusal(join_path(out, "x.png"), { part: "editor" }),
          "qt.capture: this window has no `editor`; its parts are act_quit, app, book, ed, first, go, heading, note, second")
    check("a menu item is not drawn on its own", refusal(join_path(out, "x.png"), { part: "act_quit" }),
          "qt.capture: `act_quit` is a QAction, which is not drawn on its own; capture the part that shows it")
    check("a part on a tab that is not selected", refusal(join_path(out, "x.png"), { part: "second" }),
          "qt.capture: `second` is not showing (a tab that is not selected, a closed dock or a hidden part), so it has nothing to draw")
    check("a misspelt option is refused by name", refusal(join_path(out, "x.png"), { scal: 2 }),
          "qt.capture does not know the option `scal` -- did you mean `scale`?")
    check("an option nothing is near", refusal(join_path(out, "x.png"), { format: "jpeg" }),
          "qt.capture does not know the option `format`. It takes part and scale")
    check("scale 0", refusal(join_path(out, "x.png"), { scale: 0 }),
          "qt.capture expects `scale` as a number above 0 and at most 8")
    check("scale 9", refusal(join_path(out, "x.png"), { scale: 9 }),
          "qt.capture expects `scale` as a number above 0 and at most 8")
    check("scale as text", refusal(join_path(out, "x.png"), { scale: "2" }),
          "qt.capture expects `scale` as a number above 0 and at most 8")
    check("part as a number", refusal(join_path(out, "x.png"), { part: 3 }),
          "qt.capture expects `part` as a part's name, as text")
    check("options not a record", refusal(join_path(out, "x.png"), "heading"),
          "qt.capture expects its options as a record -- `qt.capture(app, \"shot.png\", { part: \"results\", scale: 2 })`")
    check("a number is not a file", refusal(42, {}),
          "qt.capture expects the file to write as a path string or file reference")
    check("an interior NUL is refused as {file} refuses it", refusal("x" + chr(0) + ".png", {}),
          "qt.capture: a path cannot contain an interior NUL byte")
    missing = join_path(join_path(out, "no_such_folder"), "x.png")
    check("a folder that is not there fails as `write` does", refusal(missing, {}),
          "could not write file: " + missing + " (No such file or directory)")
    check("and nothing was written by any refusal", exists({file}join_path(out, "x.png")), false)

    print "-- A FOCUSED EDITOR WHILE QT'S LOOP RUNS"
    qt.invoke(app, "ed", "setFocus")
    qt.run()
    check("twelve captures were taken", count(g.steady), 12)
    check("and the caret's blink is not in any of them: one picture", count(unique(g.steady)), 1)

    qt.close(app)
    check("a closed window is refused", refusal(join_path(out, "x.png"), {}), "window `app` is not open")

    print "ok " + string(g.checks) + " checks, " + string(g.bad) + " mismatches"
end program
