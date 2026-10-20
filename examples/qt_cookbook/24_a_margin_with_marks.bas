' A MARGIN WITH MARKS -- line numbers, a mark per line, and a breakpoint you
' can click to set. Under QT_QPA_PLATFORM=offscreen, so it runs with no screen.
'
' IT IS NOT A RULER, which is the whole reason the margin carries categories
' rather than a line-number flag: a `section` mark says "this is a thing you
' can Run and it starts here", an `error` mark is the only thing in the window
' saying WHERE a syntax error is, and a `break` mark is a breakpoint somebody
' set. The three diagnostic categories are gBASIC's own severities, so a
' program showing diagnostics has one category per severity.

window ed( title: "Editor", width: 620, height: 420 )
    column
        source code( language: "gBASIC", line_numbers: true, marks: true )
        text hint( label: "click the margin to set a breakpoint" )
    end column

    ' THE MARGIN IS AN EVENT, NOT A SIGNAL -- it is a plain child widget and
    ' emits nothing -- and the handler ASKS which line it was, the rule
    ' `qt.wheel()` already follows.
    on code.gutter
        c = qt.gutter_click()
        b = qt.marks(ed, "code", "break")
        if contains(b, c.line) then
            qt.mark(ed, "code", "break", b excluding [c.line])
        else
            qt.mark(ed, "code", "break", concat(b, [c.line]))
        end if
        append(g.clicked, c.line)
    end on
end window

program main( args )
    g = { clicked: [] }
    if not qt.can_display() then
        print "no display -- nothing to show"
        return
    end if

    qt.open(ed)
    qt.write(ed, "code", join([ "function total(rows)",
                                "    t = 0",
                                "    for each r in rows",
                                "        t = t + r.amount",
                                "    end for",
                                "    return t",
                                "end function" ], chr(10)))

    ' WHAT THE MARGIN IS, or `unknown` for a `source` that has none.
    gut = qt.gutter(ed, "code")
    print "numbers: " + string(gut.line_numbers) + "   marks: " + string(gut.marks)
    print "reserved a width:            " + string(gut.width > 0)

    ' A SECTION STARTS AT LINE 1 -- 1-BASED, like `source_outline`'s own
    ' `start_line` and like every diagnostic gBASIC prints, so a line number
    ' read out of one goes straight in here with no arithmetic.
    qt.mark(ed, "code", "section", [1])
    qt.mark(ed, "code", "warning", [4])
    print "section lines:               " + string(qt.marks(ed, "code", "section"))
    print "warning lines:               " + string(qt.marks(ed, "code", "warning"))

    ' SETTING A CATEGORY REPLACES IT, which is what a gutter recomputed on
    ' every redraw wants: there is no list of what was already drawn to keep.
    qt.mark(ed, "code", "warning", [2, 5])
    print "warning, recomputed:         " + string(qt.marks(ed, "code", "warning"))
    qt.mark(ed, "code", "warning", [])
    print "warning, cleared:            " + string(qt.marks(ed, "code", "warning"))

    ' A BREAKPOINT, SET THE WAY A PERSON SETS ONE. `qt.gutter_press` is what
    ' stands in for the mouse; a line that is not on screen is refused,
    ' because a mouse could not land there either.
    qt.gutter_press(ed, "code", 4)
    qt.gutter_press(ed, "code", 6)
    print "lines clicked:               " + string(g.clicked)
    print "breakpoints:                 " + string(qt.marks(ed, "code", "break"))

    ' CLICKING THE SAME LINE AGAIN CLEARS IT -- the handler above decides that,
    ' not the margin: `qt.mark` replaces a list and the program says what the
    ' list is.
    qt.gutter_press(ed, "code", 4)
    print "after clicking 4 again:      " + string(qt.marks(ed, "code", "break"))

    ' A LINE CARRIES ONE GLYPH, and `break` outranks everything because a mark
    ' somebody PLACED must not be hidden by one the program COMPUTED.
    qt.mark(ed, "code", "error", [6])
    print "line 6 is both error and break, and break is what shows"

    qt.close(ed)
end program
