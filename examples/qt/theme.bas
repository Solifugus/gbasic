' THEMING -- one stylesheet for the whole application, and a widget that
' overrides it.
'
' Run it:
'     ./gbasic examples/qt/theme.bas
'     QT_QPA_PLATFORM=offscreen ./gbasic examples/qt/theme.bas check
'     ./gbasic examples/qt/theme.bas check            (no display at all)
'
' THAT THIRD FORM IS THE POINT OF HALF OF THIS: `qt.theme` NEEDS NO DISPLAY. A
' sheet set before any window exists is REMEMBERED and applied when there is a
' QApplication to put it on -- so a program themes first and opens second,
' which is the ordinary order to write it in. Refusing until a window existed
' would make the first window appear unthemed and then change.
'
' MOST OF THEMING ALREADY WORKED and that is worth saying: `styleSheet` is a
' Q_PROPERTY of QWidget, so per-widget styling has been reachable through the
' by-name route since it shipped. What that route CANNOT reach is one sheet for
' the whole application, which is what `qt.theme` is.
'
' A THEME IS TEXT, so a real program loads one from a file --
' `qt.theme(read({file}"dark.qss"))` -- and the language supplies the file
' while the platform supplies the sheet. Nothing here needs a theme format.

window styled( title: "Theme", width: 440, height: 320 )
    column( margin: 12, spacing: 10 )

        text heading( label: "Themed by the application" )
        text ordinary( label: "This label takes the application sheet." )

        ' A WIDGET'S OWN SHEET WINS. Qt resolves the cascade, so this is not
        ' something the backend arranges -- which is also why a backend that
        ' applied the app sheet by WRITING it onto every widget would pass
        ' every other check here and get this one wrong.
        text special( label: "This one overrides it.",
                      styleSheet: "color: #bf616a; font-style: italic" )

        group "Entries"
            form to edit it
                field one( label: "First" )
                field two( label: "Second" )
            end form
        end group

        row
            stretch()
            button close( text: "Close" )
        end row
    end column

    on close.clicked
        qt.stop()
    end on
end window

program main(args)
    it = { one: "", two: "" }

    ' THEMED BEFORE ANYTHING IS OPENED, and before we even know whether a
    ' window CAN be opened.
    sheet = ("QWidget { font-size: 11pt; }" +
             "QGroupBox { font-weight: bold; }" +
             "QLabel { color: #2e3440; }" +
             "QLineEdit { background: #eceff4; padding: 3px; }")
    qt.theme(sheet)

    if contains(args, "check") then
        ' THE HALF THAT NEEDS NOTHING. Asserted before `can_display` is even
        ' asked, which is what says the claim is about the sheet and not about
        ' a window.
        print "themed with no window open: " + string(len(qt.theme()) > 0)
        print "it is the sheet we set:     " + string(qt.theme() = sheet)
    end if

    if not qt.can_display() then
        print "No display, so nothing is opened -- but the theme was set and"
        print "read back above, which is the half that needs no display."
        return
    end if
    qt.open(styled)

    if contains(args, "check") then
        print "still the same after open:  " + string(qt.theme() = sheet)
        ' A WIDGET KEEPS ITS OWN, which is the check that separates a real
        ' cascade from a backend that stamped the sheet onto everything.
        print "a plain label's own sheet:  [" + qt.read(styled, "ordinary", "styleSheet") + "]"
        print "the special one's:          " + qt.read(styled, "special", "styleSheet")
        ' AND A THEME CAN CHANGE WHILE THE WINDOW IS UP.
        qt.theme(sheet + "QPushButton { padding: 6px 14px; }")
        print "changed while open:         " + string(qt.theme() != sheet)
        qt.stop()
        return
    end if
    qt.run()
end program
