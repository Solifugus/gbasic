' A CODE EDITOR -- the `source` widget, which is the one piece gBASIC Studio
' needs from this backend that nothing else here provides.
'
' Run it:
'     ./gbasic examples/qt/editor.bas
'     QT_QPA_PLATFORM=offscreen ./gbasic examples/qt/editor.bas check
'
' From the SOURCE TREE it needs to be told where the syntax definition is,
' which is the same arrangement `GBASIC_PATH=stdlib` already is:
'     GBASIC_SYNTAX_PATH=editors/kate ./gbasic examples/qt/editor.bas
' After `make install` that is unnecessary -- the install path is compiled in.
'
' It reads ITS OWN SOURCE as the sample text, so run it from the repository
' root; reading a file into an editor is what an editor does, and it means the
' text on screen is real gBASIC rather than three lines invented to look like it.
'
' WHY `source` IS A WORD AND NOT A PROPERTY. Everything else a widget carries
' goes through Qt's own QMetaObject by name, which is what keeps this backend
' to eight construction words against the 75 properties a QPushButton alone
' exposes. A HIGHLIGHTER IS NOT A PROPERTY: it is an object attached to a
' document, so there is nothing for a by-name lookup to find, however it is
' spelled. That is also why the completer Studio wants next will land here
' rather than in the declaration.
'
' A `source` IS A `memo` WITH FOUR DIFFERENCES, and this example prints all
' four beside a `memo` so they are facts rather than claims: it does not wrap,
' its tab is four columns rather than eight, its font is the system's
' monospaced one, and it carries a highlighter.
'
' AN EDITOR WITHOUT A DEFINITION IS STILL AN EDITOR. If the gBASIC definition
' is not installed, the window still opens, the text still edits, and a warning
' (2115) says so -- because an absent definition still paints every line with
' the theme's ordinary text colour, so there is nothing on screen to notice.

window app( title: "gBASIC editor", width: 760, height: 520 )

    menu "File"
        item act_open( label: "Open this program", shortcut: "Ctrl+O" )
        separator()
        item act_quit( label: "Quit", shortcut: "Ctrl+Q" )
    end menu

    menu "View"
        item act_light( label: "Light", checkable: true )
        item act_dark(  label: "Dark",  checkable: true, checked: true )
    end menu

    column( margin: 10, spacing: 8 )
        text heading( label: "A `source` widget, highlighted by its name" )

        ' THE SUBJECT. `language:` names a DEFINITION, never a file extension,
        ' and that is load-bearing rather than fussy: `*.bas` is claimed by
        ' FreeBASIC at the same priority as gBASIC, so a lookup by extension
        ' picks FreeBASIC -- with no error, and plausibly, FreeBASIC being a
        ' near neighbour. `theme:` is DECLARED because `qt.theme` sets a
        ' stylesheet, which does not move Qt's palette, so a dark application
        ' with an inferred theme gets light highlighting on a dark background.
        source code( language: "gBASIC", theme: "dark" )

        ' THE SAME TEXT IN A `memo`, for the comparison below. A memo is the
        ' right word for prose and the wrong one for code.
        memo plain( label: "" )

        row
            text shown( label: "" )
            stretch()
            button close( text: "Close" )
        end row
    end column

    on act_open.triggered
        load_self()
    end on
    on act_dark.triggered
        qt.theme("QWidget { background: #2e3440; color: #eceff4; }")
    end on
    on act_light.triggered
        qt.theme("")
    end on
    on act_quit.triggered
        qt.stop()
    end on
    on close.clicked
        qt.stop()
    end on
end window

' ITS OWN SOURCE AS THE SAMPLE, which is not a trick: reading a file into an
' editor is what an editor does, and it means the text on screen is real gBASIC
' rather than three lines invented to look like it.
function load_self()
    text = read({file}"examples/qt/editor.bas")
    qt.write(app, "code", text)
    qt.write(app, "plain", text)
    qt.write(app, "shown", string(len(split(text, chr(10)))) + " lines")
    return nothing
end function

program main(args)
    if not qt.can_display() then
        print "No display is available, so there is no editor to open."
        print "Set QT_QPA_PLATFORM=offscreen to render without one."
        return
    end if

    qt.open(app)
    load_self()

    if contains(args, "check") then
        ' WHAT IS ACTUALLY HIGHLIGHTING IT. `qt.highlighting` answers a record
        ' or `unknown`, and it reports the FILE as well as the language for a
        ' reason the language alone cannot cover: a definition the user has
        ' installed in their own home SHADOWS the one gBASIC ships and reports
        ' the identical name. Which of the two wins is the machine's business;
        ' not being able to SEE which would not be.
        h = qt.highlighting(app, "code")
        if is_unknown(h) then
            print "highlighting:   none -- the gBASIC definition is not installed"
            print "                (the editor still works; this is warning 2115)"
        else
            print "highlighting:   " + h.language
            print "from:           " + h.file
        end if
        print "build can:      " + string(qt.can_highlight())

        print "-- a source is a memo with four differences"
        ' BRACKETED: a line break continues a statement only inside brackets.
        print ("wrap:           source=" +
               string(qt.read(app, "code", "lineWrapMode")) + "  memo=" +
               string(qt.read(app, "plain", "lineWrapMode")))
        print ("tab narrower:   " +
               string(qt.read(app, "code", "tabStopDistance") <
                      qt.read(app, "plain", "tabStopDistance")))
        print ("fonts differ:   " + string(qt.read(app, "code", "font") !=
                                           qt.read(app, "plain", "font")))
        print "memo has none:  " + string(is_unknown(qt.highlighting(app, "plain")))

        print "-- and it is an editor, not a view"
        print "read back:      " + string(len(qt.read(app, "code"))) + " bytes"
        print "editable:       " + string(not qt.read(app, "code", "readOnly"))

        ' THE HONEST LIMITS, printed rather than left in a comment, because
        ' somebody evaluating this for an editor needs to know what is missing
        ' before they start: Qt has no line-number gutter (it is a painted side
        ' area, a separate increment), and completion needs a QCompleter --
        ' which is another object attached to a widget, so it lands exactly
        ' where the highlighter did and for the same reason. The DATA for it
        ' already exists: `source_outline` and src/lsp/.
        print "-- not yet: a line-number gutter, and completion"
        qt.stop()
        return
    end if
    qt.run()
end program
