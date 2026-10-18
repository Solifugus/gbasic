' MENUS -- a menu bar, submenus, separators and items, under
' QT_QPA_PLATFORM=offscreen, which is real Qt with no display.
'
' MEASURED BEFORE A LINE WAS WRITTEN: the whole shape needs NO NEW GRAMMAR.
' `menu "File"` is the production `group "Credit"` already uses, `item open(
' label: "..." )` is the named leaf, `separator()` is the `stretch()` form, and
' a submenu is the same production recursively -- only the vocabulary refused
' them, and bison still reports zero conflicts.
'
' SELF-CHECKING AND FORCED. Every defect here leaves A MENU BAR THAT LOOKS LIKE
' A MENU BAR: an item whose shortcut never reached it is a menu entry with no
' accelerator, a submenu flattened into its parent is a longer menu, an item
' connected to nothing is one that does nothing when chosen. None raises.
'
' AN ITEM IS A QAction AND NOT A WIDGET, which is the structural fact this
' increment rests on: `shortcut:`, `enabled:`, `checkable:` and `checked:` all
' reach it through the SAME by-name property door every widget option uses, so
' a class nothing in this backend anticipated needed no plumbing of its own.

function check(what, got, want)
    if string(got) = string(want) then
        print "ok   " + what
    else
        print "MISMATCH " + what + ": got " + string(got) + ", want " + string(want)
    end if
end function

window app( title: "App", width: 480, height: 300 )
    menu "File"
        item open_file( label: "Open...", shortcut: "Ctrl+O" )
        menu "Recent"
            item recent_one( label: "ledger.bas" )
        end menu
        separator()
        item quit_app( label: "Quit", shortcut: "Ctrl+Q" )
    end menu
    menu "Edit"
        item undo_it( label: "Undo", enabled: false )
        item wrap_it( label: "Word wrap", checkable: true, checked: true )
    end menu

    column( margin: 10 )
        field note( label: "Note" )
        button save( text: "Save" )
    end column

    on open_file.triggered
        print "SIGNAL open_file.triggered"
    end on
    on wrap_it.toggled
        print "SIGNAL wrap_it.toggled"
    end on
end window

program main()
    check("a window with menus opens", qt.open(app), true)

    ' ------------------------------------------------------------ THE ITEMS
    ' AN ITEM IS ADDRESSED BY NAME like any other part, which is what the
    ' registry holding QObject rather than QWidget bought -- a QAction cannot
    ' be a QWidget and had to be reachable by the same door.
    check("an item's label is its text", qt.read(app, "open_file", "text"),
          "Open...")
    check("and a second item has its own",
          qt.read(app, "quit_app", "text"), "Quit")
    check("an item inside a SUBMENU is reachable too",
          qt.read(app, "recent_one", "text"), "ledger.bas")

    ' ------------------------------------------------- PROPERTIES ON A QAction
    ' NONE OF THESE WAS ANTICIPATED ANYWHERE. A shortcut is a QKeySequence, set
    ' from text and read back as text by Qt's own conversion; `checkable` and
    ' `enabled` are QAction properties that no table in this backend lists.
    ' That is the by-name route (§1) reaching a class it was never told about.
    check("a shortcut set from text reaches the action",
          qt.read(app, "open_file", "shortcut"), "Ctrl+O")
    check("and a different item has a different one",
          qt.read(app, "quit_app", "shortcut"), "Ctrl+Q")
    check("`enabled: false` reaches it", qt.read(app, "undo_it", "enabled"),
          false)
    check("CONTROL: an item nobody disabled is enabled",
          qt.read(app, "open_file", "enabled"), true)
    check("`checkable: true` reaches it", qt.read(app, "wrap_it", "checkable"),
          true)
    check("and so does its state", qt.read(app, "wrap_it", "checked"), true)
    ' IN THE KIND QT DECLARES, not as text: `if qt.read(i, "enabled")` on the
    ' string "false" would be TRUE, every non-empty string being true here.
    check("a boolean reads back AS a boolean",
          type(qt.read(app, "wrap_it", "checked")), "boolean")

    ' ------------------------------------------------- THE NESTING ORACLE
    ' AN ITEM KEEPS ITS NAME WHEREVER ITS MENU ENDS UP, so every check above
    ' passes on a backend that hung `Recent` off the BAR instead of off `File`
    ' -- measured, that perturbation produced ZERO mismatches and nothing in
    ' this file could tell `File > Recent` from `File` and `Recent` side by
    ' side. The bar's own captions are the only thing that can.
    '
    ' AS A LIST, so it pins the COUNT and the ORDER too: a submenu that escaped
    ' appears as a third entry, and two menus that swapped appear reordered.
    bar = qt.menus(app)
    check("the bar holds exactly the two menus declared at the top level",
          len(bar), 2)
    check("in declaration order", bar[0], "File")
    check("and the second is its own", bar[1], "Edit")
    check("a SUBMENU is not on the bar, which is what says it is nested",
          contains(bar, "Recent"), false)

    ' AND ONE LEVEL IN, which closes the case that is structurally worse than a
    ' missing separator: AN ITEM ADDED TO THE WRONG MENU KEEPS ITS NAME, so
    ' nothing else in this file could see it. The entries pin the count, the
    ' order, and that `Recent` is an entry OF File rather than a menu beside
    ' it.
    file_menu = qt.menus(app, 0)
    check("File holds four entries: two items, a submenu and a line",
          len(file_menu), 4)
    check("the first is the first item declared", file_menu[0], "Open...")
    check("the second is the SUBMENU, as an entry of File", file_menu[1],
          "Recent")
    ' A SEPARATOR ANSWERS `unknown`, NOT "", AND THE DIFFERENCE IS LOAD-BEARING.
    ' It HAS no caption where `item x( label: "" )` has an empty one -- and that
    ' is not pedantry: a perturbation that skipped the separator branch let it
    ' FALL THROUGH and be added as an ACTION with empty text, which an
    ' empty-caption check could not tell from a real line, so the check passed
    ' over a menu with no separator in it. Qt is asked `isSeparator()` now.
    check("the third is the separator, which HAS no caption",
          is_unknown(file_menu[2]), true)
    check("and that is different from an empty one",
          string(file_menu[2]) = "", false)
    check("and the last item follows it", file_menu[3], "Quit")
    ' THE OTHER MENU IS SEPARATE, or "File has four entries" is satisfied by a
    ' backend that put every item in the first menu it made.
    check("the second menu holds only its own", len(qt.menus(app, 1)), 2)
    check("and they are its own", qt.menus(app, 1)[0], "Undo")

    ' -------------------------------------------------------- THE MENU BAR
    ' A MENU IS NOT LAID OUT, so a window holding only menus and a column must
    ' still lay the column out -- the bar is inserted at the top of the
    ' window's own layout rather than taking a place in the declaration's tree.
    ' Asserted through a widget that IS laid out, since nothing else here would
    ' notice a bar that had swallowed the layout.
    check("the laid-out parts still exist beside the bar",
          qt.read(app, "save", "text"), "Save")
    check("and a field beside them", qt.write(app, "note", "typed"), true)
    check("which reads back", qt.read(app, "note"), "typed")

    ' --------------------------------------------------------- THE HANDLERS
    ' PROVEN BY FIRING, not by the window opening: `trigger` is a no-arg QAction
    ' slot, so choosing a menu entry is driven with no human in front of it.
    ' Every check above passes on a backend that connected nothing.
    check("invoking an item by name answers",
          qt.invoke(app, "open_file", "trigger"), true)
    ' AND A CHECKABLE ITEM'S OWN SIGNAL, which is a different signal on the
    ' same class -- so "a handler on an item works" is not satisfied by one
    ' signal happening to be wired.
    check("toggling a checkable item answers",
          qt.invoke(app, "wrap_it", "toggle"), true)
    check("and toggling really changed its state",
          qt.read(app, "wrap_it", "checked"), false)

    ' ------------------------------------------------------------- REFUSALS
    on error goto next
    x = qt.read(app, "Recent", "text")
    if error then
        print "NOTADDRESSABLE: " + error.message
        error.clear()
    else
        print "MISMATCH a menu answered to its caption as if it were a part"
    end if
end program
