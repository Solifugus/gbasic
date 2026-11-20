' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' Increment 4 of the tui backend, driven by the keys and presses a person
' would use (tests/run_ui_tui.sh, tier parts): a table and a tree walked by
' arrows, a choice's list opened and picked, the menu bar by F10 and by Alt,
' pages turned by Ctrl+PgDn, a row clicked, a toolbar item pressed. Each line
' printed is what gBASIC observes; the suite holds them to what each key
' must do, written by hand.
window app( title: "Parts" )
    menu "File"
        item open( label: "Open", shortcut: "Ctrl+O" )
        separator()
        menu "Recent"
            item one( label: "one.txt" )
        end menu
        item wrap( label: "Wrap", checkable: true )
    end menu
    menu "Edit"
        item undo( label: "Undo" )
    end menu
    toolbar tools
        item save( label: "Save" )
    end toolbar
    tabs book
        tab "Orders"
            table orders to show rows
                field id( label: "Id", align: "right" )
                field who( label: "Who" )
            end table
            choice pick( label: "Pick", options: ["red", "green", "blue"] )
        end tab
        tab "Files"
            tree files to show files
                field depth( depth: true )
                field fname( label: "File" )
            end tree
        end tab
    end tabs
    status
        text msg( label: "ready" )
    end status
    on orders.chosen
        print "chosen " + string(ui.selected(app, "orders"))
    end on
    on orders.activated
        print "activated " + string(ui.selected(app, "orders"))
    end on
    on pick.changed
        print "pick changed to " + ui.read(app, "pick")
    end on
    on pick.activated
        print "pick activated"
    end on
    on open.pressed
        print "open pressed"
    end on
    on one.pressed
        print "one pressed"
    end on
    on wrap.changed
        print "wrap now " + string(ui.read(app, "wrap", "checked"))
    end on
    on undo.pressed
        print "undo pressed"
    end on
    on save.pressed
        print "save pressed"
    end on
    on book.changed
        print "book page " + string(ui.which_tab())
    end on
    ' a second handler on the same event runs too, after the first
    on book.changed
        print "and the second handler"
    end on
    on files.chosen
        print "file " + ui.cell(app, "files", ui.selected(app, "files"), "fname")
    end on
end window

program main(args)
    rows = [{ id: 1, who: "Ann" }, { id: 22, who: "Bo" }, { id: 333, who: "Cy" }]
    files = [{ depth: 0, fname: "src" }, { depth: 1, fname: "a.c" }, { depth: 1, fname: "b.c" }, { depth: 0, fname: "README" }]
    ui.open(app)
    print "menus " + string(ui.menus(app))
    k = ui.key(app, "orders", "Down")
    k = ui.key(app, "orders", "Down")
    k = ui.key(app, "orders", "End")
    k = ui.key(app, "orders", "Down")
    k = ui.key(app, "orders", "Enter")
    k = ui.key(app, "orders", "Home")
    ui.click(app, "orders", 1)
    ' a choice: arrows step it, Enter opens its list, which takes the keys
    k = ui.key(app, "pick", "Down")
    k = ui.key(app, "pick", "Enter")
    k = ui.key(app, "pick", "Down")
    k = ui.key(app, "pick", "Enter")
    print "pick is " + ui.read(app, "pick")
    k = ui.key(app, "pick", "Enter")
    k = ui.key(app, "pick", "Escape")
    print "after Escape pick is " + ui.read(app, "pick")
    ' the menu bar: F10 opens File; Down skips the separator into Recent
    k = ui.key(app, "app", "F10")
    k = ui.key(app, "app", "Enter")
    k = ui.key(app, "app", "Alt+F")
    k = ui.key(app, "app", "Down")
    k = ui.key(app, "app", "Right")
    k = ui.key(app, "app", "Enter")
    k = ui.key(app, "app", "Alt+F")
    k = ui.key(app, "app", "Up")
    k = ui.key(app, "app", "Enter")
    k = ui.key(app, "app", "Alt+E")
    k = ui.key(app, "app", "Enter")
    ui.invoke(app, "save", "press")
    print "a toolbar item clicked by name: " + string(try ui.click(app, "save"))
    ' pages
    k = ui.key(app, "orders", "Ctrl+PageDown")
    k = ui.key(app, "files", "Down")
    k = ui.key(app, "files", "Down")
    print "depth of row 2 " + string(ui.depth(app, "files", 2))
    k = ui.key(app, "files", "Ctrl+PageUp")
    ui.set(app, "msg", "text", "done")
    print "status " + ui.read(app, "msg", "text")
end program
