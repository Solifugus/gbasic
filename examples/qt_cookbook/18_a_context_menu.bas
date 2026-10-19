' A CONTEXT MENU -- a right-click that offers what the clicked row can do.
'
' A `popup` IS A MENU THAT IS NOT ON THE BAR, and it spends its one slot the
' other way round from `menu`. A container carries a NAME or OPTIONS and never
' both, so `menu "File"` spends it on the caption a person reads on the bar --
' and a context menu has no bar to be read on, so what it needs is a NAME,
' which is how `qt.popup` raises it at all.
'
' DECLARING THE HANDLER IS WHAT ASKS FOR IT. Every Qt widget starts out letting
' Qt put up its own menu, and under that setting the signal below is NEVER
' EMITTED -- so this would otherwise be a handler connected to something that
' never happens, with the right-click doing nothing and nothing raised. Writing
' the handler is enough; the cost is that your menu REPLACES the one Qt would
' have shown, so a `memo` with a context handler loses its own cut and paste.
'
' THE ROW IS `qt.selected`, with nothing new to learn: Qt moves the selection on
' a right-press, so the handler asks the same question a left click asks. What
' this row OFFERS is decided by HIDING what it does not -- one menu built once,
' narrowed per row -- and `qt.popup` answers FALSE when nothing in it is left
' visible, because an empty context menu reads as a control that failed.

window app( title: "Files", width: 460, height: 300 )
    popup row_actions
        item open_it( label: "Open" )
        item rename_it( label: "Rename..." )
        separator()
        item delete_it( label: "Delete" )
    end popup

    table files to show listing
        field name( label: "Name" )
        field kind( label: "Kind" )
    end table

    on files.customContextMenuRequested
        row = g.listing[qt.selected(app, "files")]
        g.asked = row.name
        ' A FOLDER CANNOT BE OPENED OR DELETED HERE, so those two go away.
        ' `visible` is an ordinary property of a menu item.
        qt.set(app, "open_it", "visible", row.kind = "file")
        qt.set(app, "delete_it", "visible", row.kind = "file")
        g.offered = qt.popup(app, "row_actions")
    end on

    on open_it.triggered
        g.opened = g.asked
    end on
end window

program main(args)
    if not qt.can_display() then
        print "no display, so no window -- the declaration is still checked"
        return
    end if

    listing = [{ name: "ledger.bas", kind: "file" },
               { name: "reports",    kind: "folder" },
               { name: "notes.txt",  kind: "file" }]
    ' ONE RECORD, because a handler cannot rebind a plain variable in `main`.
    g = { listing: listing, asked: "", offered: false, opened: "" }

    qt.open(app)

    ' RIGHT-CLICK THE FOLDER. `qt.right_click` drives it the way a mouse does,
    ' which is how a program can check its own menu with nobody there.
    qt.right_click(app, "files", 1)
    print "right-clicked:  " + g.asked
    print "menu appeared:  " + string(g.offered)
    print "open offered:   " + string(qt.read(app, "open_it", "visible"))

    ' AND NOW A FILE, where all three apply.
    qt.right_click(app, "files", 2)
    print "right-clicked:  " + g.asked
    print "open offered:   " + string(qt.read(app, "open_it", "visible"))

    qt.invoke(app, "open_it", "trigger")
    print "opened:         " + g.opened

    ' WHAT THE MENU HOLDS, in order. A separator has no caption of its own.
    print "the menu:       " + string(qt.menus(app, "row_actions"))

    qt.close(app)
end program
