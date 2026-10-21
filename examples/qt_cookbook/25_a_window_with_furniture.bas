' A WINDOW WITH FURNITURE -- dock panes, a toolbar and a status bar. Under
' QT_QPA_PLATFORM=offscreen, so it runs with no screen.
'
' ALL THREE BELONG TO THE WINDOW rather than to a layout, which is the same
' rule a `menu` follows: a window holding any of them is a QMainWindow, and a
' QMainWindow places its dock areas, its status bar and its tool bars around
' the content itself. A window that declares none of them is untouched.

window ide( title: "Workbench", width: 880, height: 560 )
    toolbar main
        item open( label: "Open" )
        separator()
        item save( label: "Save" )
    end toolbar

    menu "File"
        item quit( label: "Quit" )
    end menu

    column
        source code( line_numbers: true )
    end column

    ' PANES IN ONE BLOCK ARE TABBED ALONG THAT EDGE, which is what an IDE does
    ' with a column of panels -- five stacked panes means the ones below the
    ' fold are invisible.
    docks right
        dock branches "Branches"
            text b( label: "main" )
        end dock
        dock results "Results"
            memo out()
        end dock
    end docks

    ' A SECOND BLOCK IS A SECOND EDGE.
    docks bottom
        dock console "Console"
            memo log()
        end dock
    end docks

    status
        text msg( label: "Ready" )
        stretch()
        text pos( label: "1:1" )
    end status
end window

program main( args )
    if not qt.can_display() then
        print "no display -- nothing to show"
        return
    end if

    qt.open(ide)

    ' WHERE A PANE IS, asked of the window rather than of the declaration.
    print "branches: " + qt.dock(ide, "branches").area + ", sharing a tab group with " + string(qt.dock(ide, "branches").tabbed_with)
    print "console:  " + qt.dock(ide, "console").area + ", sharing a tab group with " + string(qt.dock(ide, "console").tabbed_with)

    ' A STATUS BAR NEEDS NO VERB: a `text` part in it is an ordinary widget
    ' whose `text` property `qt.set` reaches, which is why there is nothing
    ' like `qt.status(...)` to learn.
    print "status says:              " + qt.read(ide, "msg", "text")
    qt.set(ide, "msg", "text", "Saved notes.bas")
    print "and after qt.set:         " + qt.read(ide, "msg", "text")

    ' A TOOLBAR HOLDS WHAT A MENU HOLDS -- the same items, so `checkable:` and
    ' `icon:` work on them too.
    print "a toolbar item's label:   " + qt.read(ide, "open", "text")
    qt.set(ide, "save", "checkable", true)
    print "and one can be checkable: " + string(qt.read(ide, "save", "checkable"))

    ' KEEP THE ARRANGEMENT. The text is Qt's own format -- store it, do not
    ' read it; `qt.dock` is what answers questions about a pane.
    saved = qt.layout(ide)
    print "the layout is text, kept as " + string(len(saved)) + " characters"

    ' A PERSON DRAGS A PANE OUT; the program puts it back.
    qt.set(ide, "branches", "floating", true)
    print "floated:                  " + string(qt.dock(ide, "branches").floating)
    print "restored:                 " + string(qt.restore_layout(ide, saved))
    print "floating now:             " + string(qt.dock(ide, "branches").floating)

    ' AND AN ARRANGEMENT SAVED BEFORE THE WINDOW CHANGED IS REPORTED, NOT
    ' RAISED -- an ordinary thing to find on startup.
    print "an unrecognised layout:   " + string(qt.restore_layout(ide, "bm90IG1pbmU="))

    qt.close(ide)
end program
