' A SHELL -- the layout an IDE has: a file list beside an editor above an
' output table, with every boundary draggable.
'
' Run it:
'     ./gbasic examples/qt/shell.bas
'     QT_QPA_PLATFORM=offscreen ./gbasic examples/qt/shell.bas check
'
' THIS SHAPE IS WHY `split` AND `scroll` EXIST, and it was derived from gBASIC
' Studio's own source rather than guessed: its shell is `gtk.paned` three times
' nested and `gtk.scrolled` five times, and nothing in the vocabulary could
' express either. `row` and `column` are BOXES -- they pack things and nobody
' can drag the boundary -- and while `source`, `memo` and `table` scroll
' themselves, an ordinary column of widgets does not.
'
' A SPLIT'S DIRECTION IS A WORD, NOT A NAME. `across` or `down`, refused by
' name with a near-miss hint if it is anything else. The reason is measured: a
' container carries a name OR options and never both (§0.8), so there is one
' free slot, and a splitter with no direction is not a splitter -- where a name
' is something only some splitters ever want.
'
' WHICH MEANS A SPLIT CANNOT BE NAMED, so nothing addresses it and nothing
' could say whether it was built, which way round it is, or how many panes it
' got. `qt.pane` is how you see one: it walks up from a part you CAN name and
' asks the LAYOUT, which is the rule `qt.at` already follows for a grid.
'
' A SCROLL NEEDS NO DIRECTION, so its slot is free and it MAY be named -- which
' is what makes `widgetResizable` and the scrollbar policies readable by name.
'
' AND CTRL + WHEEL ZOOMS THE EDITOR, because a shell without that is not one.
' A wheel is an EVENT and not a signal -- a Qt widget receives one rather than
' emitting it -- so `on code.wheel` is handled the way `on <window>.closed` is
' and the handler asks `qt.wheel()` for the event. The zoom itself needs no
' verb: `font` reads back as text, so the size is a number in a list. And
' `qt.swallow()` is what stops the editor ALSO scrolling, which is not optional
' -- Ctrl + wheel scrolls a text view FASTER than a plain wheel, so the view
' would jump about while the text resized.
window shell( title: "Shell", width: 820, height: 520 )
    menu "File"
        item act_open( label: "Open", shortcut: "Ctrl+O" )
        separator()
        item act_quit( label: "Quit", shortcut: "Ctrl+Q" )
    end menu

    split across
        ' THE NAV PANE. A column of labels does not scroll on its own, which is
        ' exactly what `scroll` is for.
        scroll files
            column( margin: 6, spacing: 2 )
                text f1( label: "main.bas" )
                text f2( label: "shell.bas" )
                text f3( label: "studio.bas" )
                text f4( label: "widgets.bas" )
                text f5( label: "window.bas" )
                stretch()
            end column
        end scroll

        split down
            ' A `source` IS A PANE DIRECTLY -- no label, because a pane has
            ' nowhere to put one. `source code()` with empty parentheses is
            ' legal (and was a parse error until this increment, found because
            ' this is the natural thing to write).
            source code()
            table out to show lines
                field at(   label: "Line" )
                field says( label: "Message" )
            end table
        end split
    end split

    on act_open.triggered
        g.log = g.log + "opened" + chr(10)
        x = qt.write(shell, "code", ("' " + qt.read(shell, "f1") + chr(10) +
                     "print hello" + chr(10)))
    end on

    on act_quit.triggered
        qt.stop()
    end on

    on out.itemSelectionChanged
        at = qt.selected(shell, "out")
        if not is_unknown(at) then
            g.log = g.log + "picked row " + string(at) + chr(10)
        end if
    end on

    on code.wheel
        ev = qt.wheel()
        if not ev.ctrl then
            ' DECLINED, so the editor scrolls as it always did.
            return
        end if
        f = split(qt.read(shell, "code", "font"), ",")
        bigger = 1
        if ev.dy < 0 then
            bigger = 0 - 1
        end if
        qt.set(shell, "code", "font", f[0] + "," + string(number(f[1]) + bigger))
        g.zooms = g.zooms + 1
        qt.swallow()
    end on
end window

program main(args)
    g = { log: "", zooms: 0 }

    if not qt.can_display() then
        print "No display is available, so there is no window to open."
        print "Set QT_QPA_PLATFORM=offscreen to render without one."
        return
    end if

    lines = [ { at: "1", says: "ready" },
              { at: "2", says: "no errors" } ]

    qt.open(shell)
    x = qt.write(shell, "code", "x = 1" + chr(10) + "print x" + chr(10))

    if contains(args, "check") then
        print "-- the shell's regions, asked of the layout"
        nav = qt.pane(shell, "files")
        print("the file list is pane " + string(nav.index) + " of " +
              string(nav.panes) + " in the " + nav.direction + " split")
        ' TWO PANES IS THE NESTING CHECK: the outer split holds the file list
        ' AND the inner split, so an inner one hung off the window instead of
        ' its parent would report ONE here and every other line would still
        ' read correctly.
        ed = qt.pane(shell, "code")
        tb = qt.pane(shell, "out")
        print("the editor is pane " + string(ed.index) + " of " +
              string(ed.panes) + " in the " + ed.direction + " split")
        print "the table is pane  " + string(tb.index)

        print "-- ctrl + wheel over the editor"
        was = number(split(qt.read(shell, "code", "font"), ",")[1])
        claimed = qt.scroll(shell, "code", 2, "ctrl")
        print("claimed " + string(claimed) + ", font " + string(was) + " -> " +
              string(number(split(qt.read(shell, "code", "font"), ",")[1])))
        ' A PLAIN WHEEL IS LEFT ALONE, which is what keeps the editor usable.
        print("a plain wheel claims nothing: " +
              string(qt.scroll(shell, "code", -1)))

        print "-- the nearest region wins"
        deep = qt.pane(shell, "f1")
        print("a file label reports the " + deep.kind +
              ", resizable " + string(deep.resizable))
        ' QT DEFAULTS `widgetResizable` TO FALSE, and left alone the inner
        ' widget keeps its original size for ever -- so content never reflows
        ' and a resized window shows a stripe of widgets beside a grey
        ' expanse, with nothing raised.
        print("and by name:           " +
              string(qt.read(shell, "files", "widgetResizable")))

        print "-- and it all works"
        e = qt.refresh(shell, "out")
        print "the table drew " + string(e) + " rows"
        qt.invoke(shell, "act_open", "trigger")
        qt.select(shell, "out", 1)
        print "the log says: " + replace(g.log, chr(10), " / ")
        print "the editor now starts: " + left(qt.read(shell, "code"), 12)
        qt.stop()
        return
    end if

    print "Drag the boundaries. File > Open loads the first name; Ctrl+Q quits."
    qt.run()
end program
