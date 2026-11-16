' MENUS -- a bar, submenus, separators, shortcuts, and items that know whether
' they can be used.
'
' Run it:
'     ./gbasic examples/ui/menus.bas
'     QT_QPA_PLATFORM=offscreen ./gbasic examples/ui/menus.bas check
'
' THEY COST NO GRAMMAR AT ALL, which was measured before a line was written:
' `menu "File"` is the production `group "Credit"` already used, `item open(...)`
' is the named leaf, `separator()` is the `stretch()` form, and a submenu is the
' same production again one level down. Every shape already PARSED -- only the
' vocabulary refused them, so bison reports the same zero conflicts before and
' after. (`table` needed new grammar for its name; menus needed none.)
'
' A BAR IS NOT LAID OUT. A `menu` belongs to the WINDOW or to another menu, and
' nowhere else -- so it is not in the row/column tree at all, and putting one
' inside a `row` is refused at LOAD time rather than rendering a menu bar in the
' middle of the screen.
'
' AN ITEM IS A QAction, NOT A WIDGET, and that is why `shortcut:`,
' `checkable:`, `checked:` and `enabled:` all work with nothing in the backend
' naming them: they are Q_PROPERTYs, so they go through the same by-name door a
' widget property goes through. Nothing here was plumbed.
'
' A MENU TAKES NO NAME AND AN ITEM MUST HAVE ONE, which is the opposite of a
' `table` and is deliberate: a menu's CAPTION is what a person reads, and its
' ITEMS are what a handler addresses.

window app( title: "Menus", width: 520, height: 360 )

    menu "File"
        item act_new(  label: "New",  shortcut: "Ctrl+N" )
        item act_open( label: "Open", shortcut: "Ctrl+O" )
        ' A SEPARATOR IS A LINE AND NAMES NOTHING, the rule `stretch` follows
        ' for being pure space.
        separator()
        ' DISABLED IN THE DECLARATION, because there is nothing to save yet.
        ' Enabled below by `ui.set` once there is -- which is the whole reason
        ' the run-time property door exists: the declaration is written before
        ' anything has happened.
        item act_save( label: "Save", shortcut: "Ctrl+S", enabled: false )
        separator()
        item act_quit( label: "Quit", shortcut: "Ctrl+Q" )
    end menu

    menu "View"
        ' CHECKABLE ITEMS, which are a QAction's own properties.
        item view_wrap(  label: "Wrap long lines", checkable: true, checked: true )
        item view_lines( label: "Line numbers",    checkable: true )
        separator()
        ' A SUBMENU -- the same production recursively, which is why it cost
        ' nothing. It is also the shape that caught a defect: a by-name
        ' registry is FLAT, so a submenu hung off the BAR instead of inside
        ' `View` produced ZERO mismatches until `ui.menus` could be asked for
        ' the structure.
        menu "Theme"
            item theme_light( label: "Light", checkable: true, checked: true )
            item theme_dark(  label: "Dark",  checkable: true )
        end menu
    end menu

    menu "Help"
        item act_about( label: "About" )
    end menu

    column( margin: 10, spacing: 8 )
        text heading( label: "Use the menu bar, or the shortcuts" )
        memo log( read_only: true )
        row
            field entry( label: "Type to enable Save" )
            stretch()
            button close( text: "Close" )
        end row
    end column

    ' TYPING ENABLES Save, which is the ordinary reason a menu item's state
    ' changes: it is a fact about the program, not about the declaration.
    on entry.changed
        app.set("act_save", "enabled", len(app.read("entry")) > 0)
    end on

    on act_new.pressed
        note("New")
    end on
    on act_open.pressed
        note("Open")
    end on
    on act_save.pressed
        note("Saved")
    end on
    on view_wrap.pressed
        ' A CHECKABLE ITEM CARRIES ITS OWN STATE, read back through the same
        ' by-name door -- so the handler asks Qt rather than keeping a copy
        ' that could disagree with what the person sees ticked.
        note("Wrap is now " + string(app.read("view_wrap", "checked")))
    end on
    on theme_dark.pressed
        ui.theme("QWidget { background: #2e3440; color: #eceff4; }")
        note("Dark")
    end on
    on theme_light.pressed
        ui.theme("")
        note("Light")
    end on
    on act_about.pressed
        note("Menus cost no grammar at all.")
    end on
    on act_quit.pressed
        ui.stop()
    end on
    on close.pressed
        ui.stop()
    end on
end window

function note(line)
    g.lines = append(g.lines, line)
    app.write("log", join(g.lines, chr(10)))
    return nothing
end function

program main(args)
    ' ONE RECORD FOR THE STATE, the rule every stateful gBASIC GUI follows: a
    ' handler sees the frame that called `ui.run` and CANNOT rebind a scalar
    ' there, so warning 2104's own remedy is to mutate a field of a shared
    ' record.
    g = { lines: [] }

    if not ui.can_display() then
        print "No display is available, so there is no window to open."
        print "Set QT_QPA_PLATFORM=offscreen to render without one."
        return
    end if
    ui.open(app)

    if contains(args, "check") then
        ' THE BAR IS A TREE AND THE REGISTRY IS FLAT, so `ui.menus` is the only
        ' thing that can witness the STRUCTURE: an item in the wrong menu, a
        ' submenu hung off the bar, a missing separator -- each is invisible to
        ' a by-name check and each produced zero mismatches before this existed.
        print "the bar:        " + string(app.menus())
        print "File holds:     " + string(app.menus(0))
        print "View holds:     " + string(app.menus(1))
        ' A SEPARATOR ANSWERS `unknown`, NOT "". It HAS no caption, where an
        ' `item x( label: "" )` has an empty one -- and that difference is what
        ' catches a separator that silently fell through and was added as an
        ' action with no text, which a caption check cannot tell from a line.
        print("entry 2 of File is a separator: " +
              string(is_unknown(app.menus(0)[2])))

        print "-- an item is a QAction, so its properties come by name"
        print "Save's shortcut: " + app.read("act_save", "shortcut")
        print "Save enabled:    " + string(app.read("act_save", "enabled"))
        app.write("entry", "something")
        print "after typing:    " + string(app.read("act_save", "enabled"))
        print "Wrap checkable:  " + string(app.read("view_wrap", "checkable"))
        print "Wrap checked:    " + string(app.read("view_wrap", "checked"))

        print "-- and a handler on a menu item fires like any other"
        app.invoke("act_open", "press")
        ' `trigger` IS WHAT A CLICK DOES, and on a CHECKABLE action it flips
        ' the state as well as firing -- which is why the handler can read the
        ' new value back rather than keeping a copy. (`toggle` flips it and
        ' emits `toggled`, which is a DIFFERENT signal and would not reach a
        ' handler written for `triggered` -- measured, by writing it that way
        ' first and getting one log line where two were wanted.)
        app.invoke("view_wrap", "press")
        print "wrap after a click: " + string(app.read("view_wrap", "checked"))
        print "the log says:    " + string(len(g.lines)) + " line(s)"
        print "the last one:    " + g.lines[len(g.lines) - 1]
        ui.stop()
        return
    end if
    ui.run()
end program
