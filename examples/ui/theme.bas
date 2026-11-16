' THEMING -- one stylesheet for the whole application, and a widget that
' overrides it.
'
' Run it:
'     ./gbasic examples/ui/theme.bas
'     QT_QPA_PLATFORM=offscreen ./gbasic examples/ui/theme.bas check
'     ./gbasic examples/ui/theme.bas check            (no display at all)
'
' THAT THIRD FORM IS THE POINT OF HALF OF THIS: `ui.theme` NEEDS NO DISPLAY. A
' sheet set before any window exists is REMEMBERED and applied when there is a
' QApplication to put it on -- so a program themes first and opens second,
' which is the ordinary order to write it in. Refusing until a window existed
' would make the first window appear unthemed and then change.
'
' ONE PART CAN STILL STAND OUT: `emphasis: "outline"` (or "strong") marks it
' in the theme's accent, on every backend.
'
' A THEME IS TEXT, so a real program loads one from a file --
' `ui.theme(read({file}"dark.qss"))` -- and the language supplies the file
' while the platform supplies the sheet. Nothing here needs a theme format.

window styled( title: "Theme", width: 440, height: 320 )
    column( margin: 12, spacing: 10 )

        text heading( label: "Themed by the application" )
        text ordinary( label: "This label takes the application sheet." )

        ' ONE PART MARKED, in the accent. It must not leak onto its
        ' neighbours, which is the check below.
        text special( label: "This one stands out.", emphasis: "outline" )

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

    on close.pressed
        ui.stop()
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
    ui.theme(sheet)

    if contains(args, "check") then
        ' THE HALF THAT NEEDS NOTHING. Asserted before `can_display` is even
        ' asked, which is what says the claim is about the sheet and not about
        ' a window.
        print "themed with no window open: " + string(len(ui.theme()) > 0)
        print "it is the sheet we set:     " + string(ui.theme() = sheet)
    end if

    if not ui.can_display() then
        print "No display, so nothing is opened -- but the theme was set and"
        print "read back above, which is the half that needs no display."
        return
    end if
    ui.open(styled)

    if contains(args, "check") then
        print "still the same after open:  " + string(ui.theme() = sheet)
        ' THE MARK IS ON ONE PART, which is the check that separates it from
        ' a backend that stamped it onto everything.
        print "a plain label's emphasis:   " + styled.read("ordinary", "emphasis")
        print "the special one's:          " + styled.read("special", "emphasis")
        ' AND A THEME CAN CHANGE WHILE THE WINDOW IS UP.
        ui.theme(sheet + "QPushButton { padding: 6px 14px; }")
        print "changed while open:         " + string(ui.theme() != sheet)
        ui.stop()
        return
    end if
    ui.run()
end program
