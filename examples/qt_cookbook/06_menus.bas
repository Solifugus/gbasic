' A MENU BAR, SUBMENUS, SEPARATORS AND SHORTCUTS -- and it cost no grammar.
'
' Every shape here was already a production the vocabulary had: `menu "File"` is
' what `group "Credit"` uses, `item open(...)` is the named leaf, `separator()`
' is `stretch()`, and a submenu is the same production once more.
'
' A MENU BELONGS TO THE WINDOW, not to a layout: putting one inside a `row`
' would render a menu bar in the middle of the screen, so it is refused.
' An ITEM IS A QAction rather than a widget, which is why `shortcut:`,
' `checkable:` and `enabled:` all work with nothing in the backend naming them
' -- they are ordinary properties reached by name.
window editor( title: "Menus", width: 480, height: 300 )

    menu "File"
        item act_new(  label: "New",  shortcut: "Ctrl+N" )
        item act_open( label: "Open", shortcut: "Ctrl+O" )
        separator()
        ' DISABLED IN THE DECLARATION, because there is nothing to save yet.
        item act_save( label: "Save", shortcut: "Ctrl+S", enabled: false )
    end menu

    menu "View"
        item wrap( label: "Wrap long lines", checkable: true, checked: true )
        menu "Theme"
            item light( label: "Light", checkable: true, checked: true )
            item dark(  label: "Dark",  checkable: true )
        end menu
    end menu

    column( margin: 10 )
        field title( label: "Title" )
        memo  body(  label: "" )
    end column

    ' TYPING ENABLES Save, which is the ordinary reason a menu item's state
    ' changes: a fact about the program rather than about the declaration.
    on title.textChanged
        editor.set("act_save", "enabled", len(editor.read("title")) > 0)
    end on
end window

program main(args)
    if not qt.can_display() then
        print "no display"
        return
    end if
    qt.open(editor)

    ' THE BAR IS A TREE AND THE PARTS REGISTRY IS FLAT, so `qt.menus` is the
    ' only thing that can witness the STRUCTURE -- an item in the wrong menu or
    ' a submenu hung off the bar is invisible to a lookup by name.
    print "the bar:        " + string(editor.menus())
    print "File holds:     " + string(editor.menus(0))
    print "View holds:     " + string(editor.menus(1))
    ' A SEPARATOR ANSWERS `unknown`, NOT "": it HAS no caption, where an item
    ' with `label: ""` has an empty one.
    print ("entry 2 of File is a separator: " +
           string(is_unknown(editor.menus(0)[2])))

    print "Save's shortcut: " + editor.read("act_save", "shortcut")
    print "Save enabled:    " + string(editor.read("act_save", "enabled"))
    editor.write("title", "notes.txt")
    print "after typing:    " + string(editor.read("act_save", "enabled"))
    qt.stop()
end program
