' A FILE BROWSER WITH A CONTEXT MENU -- the other one you can sit in front of
' and use. Right-click a row.
'
' Run it:
'     ./gbasic examples/qt/browser.bas
'     QT_QPA_PLATFORM=offscreen ./gbasic examples/qt/browser.bas check
'
' WHY THIS SHAPE. It is Studio's browser, which is what the `popup` word was
' built for: a list of files where a right-click offers what THAT row can do --
' a folder cannot be opened in an editor and the project row cannot be deleted
' -- so the menu is narrowed per row rather than being a fixed list with items
' that do nothing.
'
' ONE MENU, BUILT ONCE, NARROWED PER ROW. The declaration holds every item
' there is and the handler HIDES the ones this row does not offer, which is
' exactly what Studio does and for the reason its own source gives: rebuilding
' the menu per right-click would reconnect every handler every time. `visible`
' is an ordinary property of a menu item, so this needed no new verb.
'
' AND `qt.popup` ANSWERS WHETHER ANYTHING SHOWED. A row that offers nothing
' leaves every item hidden, and an empty menu reads as a control that failed --
' so the answer is `false` and the program simply does not raise one, which is
' the project row below.
'
' DECLARING THE HANDLER IS WHAT ASKS FOR THE MENU. Qt puts up its own by
' default and emits no signal while it does, so a handler without the policy
' set would be connected to something that never happens. Writing `on
' files.customContextMenuRequested` is the whole request -- the cost being that
' this REPLACES Qt's own menu on that widget.

window app( title: "Browser", width: 620, height: 420 )
    popup row_actions
        item open_it(    label: "Open" )
        item rename_it(  label: "Rename..." )
        item new_here(   label: "New File here" )
        separator()
        item delete_it(  label: "Delete" )
        menu "More"
            item info_it( label: "Properties" )
        end menu
    end popup

    column( margin: 10, spacing: 8 )
        text intro( label: "Right-click a row. What it offers depends on what it is." )
        split across
            table files to show listing
                field name( label: "Name" )
                field kind( label: "Kind" )
            end table
            scroll side
                column( margin: 6, spacing: 6 )
                    text header( label: "Log" )
                    memo log( readOnly: true )
                end column
            end scroll
        end split
    end column

    on files.customContextMenuRequested
        ' QT MOVED THE SELECTION ON THE RIGHT-PRESS, so this is the row under
        ' the pointer and not wherever the selection happened to be.
        at = qt.selected(app, "files")
        if is_unknown(at) then
            return
        end if
        row = g.listing[at]
        g.clicked = row.name

        is_file = row.kind = "file"
        is_dir = row.kind = "folder"
        qt.set(app, "open_it",   "visible", is_file)
        qt.set(app, "rename_it", "visible", is_file or is_dir)
        qt.set(app, "delete_it", "visible", is_file or is_dir)
        qt.set(app, "new_here",  "visible", is_dir)
        qt.set(app, "info_it",   "visible", is_file or is_dir)

        g.offered = qt.popup(app, "row_actions")
        if g.offered then
            g.lines = append(g.lines, ("right-click on " + row.name +
                                       " (" + row.kind + ")"))
        else
            ' NOTHING TO OFFER. Said in the log rather than shown as an empty
            ' menu, which is what the `false` is for.
            g.lines = append(g.lines, row.name + " offers nothing")
        end if
        x = qt.write(app, "log", join(g.lines, chr(10)))
    end on

    on open_it.triggered
        g.lines = append(g.lines, "  -> open " + g.clicked)
        x = qt.write(app, "log", join(g.lines, chr(10)))
    end on

    on rename_it.triggered
        g.lines = append(g.lines, "  -> rename " + g.clicked)
        x = qt.write(app, "log", join(g.lines, chr(10)))
    end on

    on new_here.triggered
        g.lines = append(g.lines, "  -> new file in " + g.clicked)
        x = qt.write(app, "log", join(g.lines, chr(10)))
    end on

    on delete_it.triggered
        g.lines = append(g.lines, "  -> delete " + g.clicked)
        x = qt.write(app, "log", join(g.lines, chr(10)))
    end on

    on info_it.triggered
        g.lines = append(g.lines, "  -> properties of " + g.clicked)
        x = qt.write(app, "log", join(g.lines, chr(10)))
    end on
end window

program main(args)
    if not qt.can_display() then
        print "No display is available, so there is no window to open."
        print "Set QT_QPA_PLATFORM=offscreen to render without one."
        return
    end if

    listing = [{ name: "ledger.bas",  kind: "file" },
               { name: "reports",     kind: "folder" },
               { name: "notes.txt",   kind: "file" },
               { name: "(project)",   kind: "project" }]
    ' ONE RECORD, because a handler cannot rebind a plain variable in `main`.
    g = { listing: listing, lines: [], clicked: "", offered: false }

    qt.open(app)

    if contains(args, "check") then
        ' WHAT A PERSON WOULD DO, driven. `qt.right_click` sends a press, a
        ' release and the context event in the order a mouse produces them --
        ' which is also the only way to reach a table at all, since a table
        ' answers a right-click on its scrolling area rather than on the
        ' widget, and nothing in gBASIC can name that.
        qt.right_click(app, "files", 1)
        print "folder:   clicked " + g.clicked + ", menu " + string(g.offered)
        print ("          open offered " +
               string(qt.read(app, "open_it", "visible")) +
               ", new-here offered " +
               string(qt.read(app, "new_here", "visible")))

        qt.right_click(app, "files", 0)
        print "file:     clicked " + g.clicked + ", menu " + string(g.offered)
        print ("          open offered " +
               string(qt.read(app, "open_it", "visible")) +
               ", new-here offered " +
               string(qt.read(app, "new_here", "visible")))
        qt.invoke(app, "open_it", "trigger")

        qt.right_click(app, "files", 3)
        print "project:  clicked " + g.clicked + ", menu " + string(g.offered)

        print "the menu: " + string(qt.menus(app, "row_actions"))
        print "the bar:  " + string(qt.menus(app))
        print "log:"
        for each line in g.lines
            print "  " + line
        end for
        qt.close(app)
        return
    end if

    print "Right-click a row. Close the window to finish."
    qt.run()
end program
