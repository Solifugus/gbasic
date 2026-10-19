' A CONTEXT MENU (§0.20) -- `popup`, `qt.popup`, `qt.right_click`, and the
' policy a declared handler asks for. Under QT_QPA_PLATFORM=offscreen: real
' QMenus and real mouse events, no display.
'
' WHY IT IS A WORD AND NOT A PROPERTY, measured before anything was written.
' `QMenu::popup` and `QMenu::exec` are NOT invokable by name (both -1 against
' `show` at 18 and `hide` at 19), so `qt.invoke` cannot reach either and
' `show()` would put the menu wherever it last was rather than where the
' pointer is. And a `menu` cannot be reused: it spends its one slot on the
' caption a person reads on the bar (§0.8 measured that a container carries a
' name OR options, never both), where a context menu has no bar to be read on
' and needs a NAME so `qt.popup` can raise it. The same trade `split across`
' makes in the other direction.
'
' SELF-CHECKING NOT GOLDEN AND FORCED. Every defect here is an ORDINARY-LOOKING
' RUN: a policy left at Qt's default gives a handler connected to a signal
' nothing ever emits, so the right-click simply does nothing and raises
' nothing; an item built into the wrong popup of two keeps its name, so every
' by-name check passes; a popup quietly added to the menu bar renders as an
' extra bar entry nobody reads; and a right-click that fires the signal without
' moving the selection hands the handler a stale row, which is a plausible
' answer about the wrong file.
'
' THE TIER TO READ FIRST IS VIEWPORT, because it is the measurement the whole
' increment turns on: a `table`, `memo` or `source` does NOT answer a
' context-menu event sent to the widget itself. Delivered to the QTableWidget
' the signal fired 0 times and the event propagated to the PARENT; delivered to
' that widget's VIEWPORT it fired 1. A real right-click lands on the viewport,
' so the feature works for a person on all five kinds -- and gBASIC cannot name
' a viewport, so without `qt.right_click` routing there the path is drivable
' only on `field` and `button`, the two kinds that happen to have none.
'
' AND THE ROW CAME FREE. I expected to have to deliver the signal's QPoint, or
' to select the row under the pointer as a backend policy. Measured, Qt MOVES
' THE CURRENT ROW on a right-press by itself, so `qt.selected` already answers
' "which row was right-clicked" -- the fifth time in this backend that
' measuring a gap before filling it found it closed.

function check(what, got, want)
    if string(got) = string(want) then
        print "ok   " + what
    else
        print "MISMATCH " + what + ": got " + string(got) + ", want " + string(want)
    end if
end function

' STUDIO'S OWN BROWSER SHAPE: a file list whose right-click offers what the
' clicked row can do, and an editor beside it. Two popups on purpose -- with
' one, an item built into the wrong menu is unobservable.
window app( title: "Context", width: 620, height: 400 )
    menu "File"
        item quit_it( label: "Quit" )
    end menu

    popup row_actions
        item open_it( label: "Open" )
        item rename_it( label: "Rename..." )
        separator()
        item delete_it( label: "Delete" )
        menu "More"
            item info_it( label: "Info" )
        end menu
    end popup

    popup editor_actions
        item comment_it( label: "Comment out" )
    end popup

    split across
        table files to show rows
            field name( label: "File" )
            field kind( label: "Kind" )
        end table
        source code( plainText: "x = 1" )
    end split

    on files.customContextMenuRequested
        g.asked = g.asked + 1
        picked = qt.selected(app, "files")
        r = g.rows[picked]
        ' WHAT STUDIO DOES: one menu built once, holding every item, with the
        ' ones this row does not offer HIDDEN. `visible` is Q_PROPERTY 14 of
        ' QAction, so this needed no new capability.
        qt.set(app, "delete_it", "visible", r.kind = "file")
        qt.set(app, "rename_it", "visible", r.kind = "file")
        qt.set(app, "open_it", "visible", r.kind = "file")
        qt.set(app, "info_it", "visible", r.kind = "file")
        g.shown = qt.popup(app, "row_actions")
    end on

    on code.customContextMenuRequested
        g.editor_asked = g.editor_asked + 1
    end on

    on open_it.triggered
        g.opened = g.opened + 1
    end on
end window

' A WINDOW WITH NO CONTEXT HANDLER AT ALL -- the control for the policy tier.
' Without it "the policy is set" is satisfied by a backend that sets it on
' every widget there is, which would silently take Qt's own cut/copy/paste
' menu away from every `memo` and `field` in the language.
window plain( title: "Plain", width: 300, height: 160 )
    column
        memo notes( plainText: "hello" )
        field who()
    end column
end window

' EVERY KIND THAT CAN BE RIGHT-CLICKED, so the viewport routing is asserted
' where it matters AND where it is not needed. `field` and `button` answer a
' direct event; `table`, `memo` and `source` answer only through a viewport,
' which is the difference this window exists to show.
window kinds( title: "Kinds", width: 420, height: 420 )
    popup any_actions
        item do_it( label: "Do it" )
    end popup
    column
        table grid to show rows
            field name( label: "Name" )
        end table
        memo note( plainText: "note" )
        source src( plainText: "y = 2" )
        field one()
        button press( label: "Press" )
    end column
    on grid.customContextMenuRequested
        g.hits = g.hits + 1
        g.where = append(g.where, "table")
    end on
    on note.customContextMenuRequested
        g.hits = g.hits + 1
        g.where = append(g.where, "memo")
    end on
    on src.customContextMenuRequested
        g.hits = g.hits + 1
        g.where = append(g.where, "source")
    end on
    on one.customContextMenuRequested
        g.hits = g.hits + 1
        g.where = append(g.where, "field")
    end on
    on press.customContextMenuRequested
        g.hits = g.hits + 1
        g.where = append(g.where, "button")
    end on
end window

program main()
    rows = [{ name: "a.bas", kind: "file" },
            { name: "src",   kind: "dir" },
            { name: "b.bas", kind: "file" }]
    ' ONE RECORD, AND THE HANDLERS MUTATE ITS FIELDS. gBASIC has no closures
    ' and a handler fires beneath `qt.run()`, so it can READ a variable in
    ' `main` and CANNOT REBIND one -- warning 2104 says so and names this
    ' remedy. The first draft of this fixture counted into plain scalars and
    ' every counter read 0 while the handlers had plainly run, which is the
    ' trap the cookbook's recipe 3 exists to teach.
    g = { asked: 0, editor_asked: 0, opened: 0, shown: false, hits: 0,
          where: [], rows: rows }

    qt.open(app)

    ' ---- the oracle: WHICH popup an item landed in ------------------------
    ' A flat by-name registry cannot witness a tree, which is the measurement
    ' the submenu oracle already rests on: an `item` keeps its name wherever
    ' its menu ends up, so one built into `editor_actions` instead of
    ' `row_actions` passes every other check in this file.
    check("row_actions entries", qt.menus(app, "row_actions"),
          ["Open", "Rename...", unknown, "Delete", "More"])
    check("editor_actions entries", qt.menus(app, "editor_actions"),
          ["Comment out"])
    ' A SEPARATOR ANSWERS `unknown` AND NOT "", which is the absent/empty
    ' distinction this language makes everywhere -- and is what caught a
    ' separator falling through as an empty-texted action one increment ago.
    check("the separator is a line", is_unknown(qt.menus(app, "row_actions")[2]),
          true)
    ' A SUBMENU OF A POPUP needed no vocabulary: the same production recursing.
    check("the submenu's own entries", qt.menus(app, 0), ["Quit"])

    ' ---- a popup is NOT on the bar ---------------------------------------
    ' The control that says `popup` is a different kind of menu rather than a
    ' spelling of `menu`. A popup added to the bar would render as an extra
    ' entry nobody reads, and every entry check above would still pass.
    check("the bar holds only the declared menu", qt.menus(app), ["File"])

    ' ---- the policy a declared handler asks for ---------------------------
    ' THE LOAD-BEARING HALF. Every widget kind starts at `DefaultContextMenu`,
    ' and under that policy `customContextMenuRequested` is NEVER EMITTED -- so
    ' without this the handler below is connected to a signal nothing emits and
    ' the right-click does nothing, silently.
    check("a part with a handler got the policy",
          qt.read(app, "files", "contextMenuPolicy"), "CustomContextMenu")
    check("so did the editor", qt.read(app, "code", "contextMenuPolicy"),
          "CustomContextMenu")

    ' ---- the right-click, and the row it lands on -------------------------
    qt.right_click(app, "files", 1)
    check("the handler fired", g.asked, 1)
    ' Qt MOVES THE SELECTION on a right-press, so this is the row under the
    ' pointer rather than wherever the selection happened to be.
    check("the selection followed the click", qt.selected(app, "files"), 1)
    ' Row 1 is a directory, so Open/Rename/Delete/Info are hidden and only the
    ' submenu's own entry remains visible -- which is what `shown` answers.
    check("a directory still offers the submenu", g.shown, true)
    check("delete is hidden for a directory",
          qt.read(app, "delete_it", "visible"), false)

    qt.right_click(app, "files", 2)
    check("a second click moved the row", qt.selected(app, "files"), 2)
    check("the handler fired again", g.asked, 2)
    check("delete is back for a file", qt.read(app, "delete_it", "visible"),
          true)

    ' AND THE ITEMS STILL WORK, which is what says the popup's actions are
    ' connected like a bar menu's rather than merely built.
    qt.invoke(app, "open_it", "trigger")
    check("the popup's item fired its handler", g.opened, 1)

    ' ---- nothing to show is `false`, not a refusal ------------------------
    ' Studio's own context menu hides every item for a row that offers
    ' nothing, and its comment says an empty menu "reads as a control that
    ' failed". Measured, Qt agrees: an all-hidden menu answers isEmpty() true
    ' and popup() leaves it invisible. BOTH halves are asserted, since "true"
    ' alone passes on a verb that always says yes.
    qt.set(app, "comment_it", "visible", false)
    check("an all-hidden popup shows nothing", qt.popup(app, "editor_actions"),
          false)
    qt.set(app, "comment_it", "visible", true)
    check("and shows again once an item is back",
          qt.popup(app, "editor_actions"), true)

    ' ---- the editor answers too -------------------------------------------
    qt.right_click(app, "code")
    check("a right-click in the editor fired", g.editor_asked, 1)

    ' ---- refusals, each beside its nearest legal neighbour ----------------
    on error goto next
    qt.popup(app, "not_there")
    check("an unknown popup is refused", error.message,
          "qt.popup: this window has no `not_there`")
    error.clear()

    qt.popup(app, "files")
    ' NAMES THE KIND IT FOUND, because the mistake is naming a table where a
    ' popup was meant and "no such thing" would be false about a name that is
    ' right there.
    check("a table is not a popup menu", error.message,
          "qt.popup: `files` is a QTableWidget, not a popup menu")
    error.clear()

    qt.right_click(app, "open_it")
    check("an item has nothing to click", error.message,
          "qt.right_click: `open_it` is a QAction, which has nothing to click")
    error.clear()

    qt.right_click(app, "files", 99)
    check("a row that is not there is refused", error.message,
          "qt.right_click: that table has no such row")
    error.clear()

    qt.right_click(app, "code", 0)
    check("only a table has rows to click", error.message,
          "qt.right_click: `code` is not a table, so it has no row to click")
    error.clear()

    qt.menus(app, "files")
    check("the oracle refuses a non-popup by name", error.message,
          "qt.menus: this window has no popup menu called `files`")
    error.clear()
    on error stop

    qt.close(app)

    ' ---- the control: no handler, no policy change ------------------------
    ' Without this, "the policy is set" is satisfied by setting it on every
    ' widget, which takes Qt's own cut/copy/paste menu away from every `memo`
    ' and `field` in the language and says nothing.
    qt.open(plain)
    check("a memo with no handler keeps Qt's own menu",
          qt.read(plain, "notes", "contextMenuPolicy"), "DefaultContextMenu")
    check("and so does a field",
          qt.read(plain, "who", "contextMenuPolicy"), "DefaultContextMenu")
    qt.close(plain)

    ' ---- TIER VIEWPORT: every kind a right-click can land on -------------
    qt.open(kinds)
    qt.right_click(kinds, "grid", 0)
    qt.right_click(kinds, "note")
    qt.right_click(kinds, "src")
    qt.right_click(kinds, "one")
    qt.right_click(kinds, "press")
    ' THE THREE SCROLL-AREA KINDS ARE THE POINT. Without viewport routing they
    ' answer NOTHING -- measured at 0 against 1 -- so a run that reports only
    ' `field` and `button` is the defect, and `hits` alone could not say which
    ' three were missing.
    check("every kind answered a right-click", g.hits, 5)
    check("and each one is the part that was clicked", g.where,
          ["table", "memo", "source", "field", "button"])
    qt.close(kinds)
end program
