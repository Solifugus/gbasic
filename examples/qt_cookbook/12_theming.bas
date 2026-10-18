' ONE STYLESHEET FOR THE WHOLE APPLICATION, and a widget that overrides it.
'
' PER-WIDGET STYLING NEEDED NOTHING NEW: `styleSheet` is an ordinary
' Q_PROPERTY of every QWidget, so it arrives through the same by-name route as
' `text` or `enabled`. What that route CANNOT reach is one sheet for the
' application, because that belongs to the QApplication rather than to any
' widget -- so `qt.theme` is the verb for it.
window panel( title: "Theming", width: 420, height: 220 )
    column( margin: 10, spacing: 8 )
        text heading( label: "Themed by the application" )
        field normal(  label: "Ordinary" )
        ' ITS OWN SHEET, declared. Qt resolves the cascade, so a widget's own
        ' sheet wins over the application's -- which is what makes an
        ' exception expressible at all.
        field special( label: "Its own colours",
                       styleSheet: "background: #ffd; color: #600;" )
        button close( text: "Close" )
    end column
end window

program main(args)
    ' IT NEEDS NO DISPLAY. A sheet set before any window exists is REMEMBERED
    ' and applied when there is a QApplication to put it on -- so a program
    ' themes first and opens second, and this half of the recipe runs on a
    ' text-only machine.
    qt.theme("QWidget { background: #2e3440; color: #eceff4; }")
    print ("a theme can be set before anything opens: " +
                 string(len(qt.theme()) > 0))

    if not qt.can_display() then
        print "no display"
        return
    end if
    qt.open(panel)

    ' THE APPLICATION'S SHEET IS WHAT WAS ASKED FOR.
    print ("the app sheet holds the dark background: " +
                 string(contains(qt.theme(), "#2e3440")))

    ' AND THE WIDGET THAT DECLARED ITS OWN STILL HAS IT. A theme applied by
    ' WRITING onto every widget would pass a check that only asked whether the
    ' app sheet was set, and would silently flatten this.
    print ("the ordinary field has no sheet of its own: " +
                 string(len(qt.read(panel, "normal", "styleSheet")) = 0))
    print ("the special one kept its own:               " +
                 string(contains(qt.read(panel, "special", "styleSheet"), "#ffd")))

    ' CHANGING IT AT RUN TIME is the same call. A dark and a light theme are
    ' two strings; there is no theme registry to keep in step.
    qt.theme("")
    print "and clearing it leaves nothing: " + string(len(qt.theme()) = 0)
    qt.stop()
end program
