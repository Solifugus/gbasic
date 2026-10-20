' TABS -- a book of pages, which is how a settings screen stops being a wall.
'
' Run it:
'     ./gbasic examples/qt/tabs.bas
'     QT_QPA_PLATFORM=offscreen ./gbasic examples/qt/tabs.bas check
'
' A `tab` NEEDS A CAPTION AND NOTHING ELSE, because the caption is what a
' person reads, so a DECLARED tab spends its one slot there and cannot also be
' named -- which §0.27 did not change: a `tab` may carry a name in a RECORD, so
' a reconcile has an identity that is not the caption, and the declaration's
' one-slot rule stands. The `tabs` itself IS named, because it has
' state a handler can ask about -- which page is showing -- and that is the
' short list of containers that may be named: a `table` and a `tabs`.
'
' A PAGE IS GIVEN A COLUMN LAYOUT, which is why everything else in the
' vocabulary works inside one with no further words: a `field`, a `group`, a
' `cells` grid and a whole `table` all just go in.
'
' AND A CONTAINER CARRIES A NAME OR OPTIONS, NOT BOTH -- `tabs settings(
' movable: true )` with a body costs four grammar conflicts, measured -- so a
' named container's properties are set at run time with `qt.set`, which this
' example does below.

window prefs( title: "Preferences", width: 460, height: 360 )
    column( margin: 10, spacing: 8 )

        tabs settings
            tab "General"
                field who(  label: "Your name" )
                check live( label: "Check for updates" )
            end tab

            tab "Appearance"
                ' A GRID INSIDE A PAGE, needing no further vocabulary.
                cells
                    row
                        text note( label: "Sizes apply after a restart",
                                   across: 2 )
                    end row
                    row
                        field size(  label: "Font size" )
                        field width( label: "Line width" )
                    end row
                end cells
            end tab

            tab "Folders"
                ' AND A WHOLE TABLE, which is the payoff of a page being a
                ' column rather than a bare widget.
                table places to show folders
                    field kind( label: "Kind" )
                    field path( label: "Where" )
                end table
            end tab

            tab "Notes"
                memo scratch( label: "Anything you like." )
            end tab
        end tabs

        row
            stretch()
            button close( text: "Close" )
        end row
    end column

    ' A `tabs` IS NAMED, so a handler can hear it change pages.
    on settings.currentChanged
        print "page is now " + string(qt.read(prefs, "settings", "currentIndex"))
    end on

    on close.clicked
        qt.stop()
    end on
end window

program main(args)
    folders = [ { kind: "Documents", path: "~/Documents" },
                { kind: "Pictures",  path: "~/Pictures" } ]
    if not qt.can_display() then
        print "No display is available, so there is no window to open."
        print "Set QT_QPA_PLATFORM=offscreen to render without one."
        return
    end if
    qt.open(prefs)

    ' PROPERTIES AT RUN TIME, which is the other half of name-or-options: the
    ' declaration could not carry `movable:` beside the name, so it is set
    ' here -- and that is a better place for it anyway, a program being able
    ' to decide.
    qt.set(prefs, "settings", "movable", true)
    qt.set(prefs, "settings", "documentMode", true)

    if contains(args, "check") then
        print "page count:        " + string(qt.read(prefs, "settings", "count"))
        print "on page:           " + string(qt.read(prefs, "settings", "currentIndex"))
        print "movable:           " + string(qt.read(prefs, "settings", "movable"))
        ' A PART ON ANY PAGE IS REACHED BY NAME, with nothing saying which page
        ' it is on -- the registry is flat, which is exactly why the tab
        ' structure needs its own oracle in the test suite.
        print "a part on page 2:  " + qt.read(prefs, "note")
        print "a part on page 4:  " + qt.read(prefs, "scratch")
        print "a cell of page 3:  " + qt.cell(prefs, "places", 1, "kind")
        ' CHANGING PAGES FROM THE PROGRAM, which fires the handler above.
        qt.set(prefs, "settings", "currentIndex", 2)
        print "after setting:     " + string(qt.read(prefs, "settings", "currentIndex"))
        qt.stop()
        return
    end if
    qt.run()
end program

' A NOTE ON WHAT IS NOT READABLE. `qt.menus` answers the MENU bar's captions
' because a menu bar is a TREE the flat by-name registry cannot witness. A tab
' bar has the same shape of problem and no verb for it: `tabText` is a METHOD
' taking an index rather than a property, so it is not reachable by name. The
' page COUNT and the CURRENT INDEX are properties and are reachable, which is
' enough for a program to drive the thing; the captions are not. Said here
' rather than left for somebody to discover.
