' WHICH THING WAS THIS EVENT ABOUT? (docs/qt_gui_design.md §0.33) --
' `qt.which_tab`, and `qt.right_click` aimed at a tree row and at a tab.
' Under QT_QPA_PLATFORM=offscreen: real widgets, real mouse events, no display.
'
' THE SHAPE IS WHAT THE gBASIC STUDIO BENCH ASKED TO HAVE NAMED, and it named
' it rather than this backend: a handler here takes no arguments, so every
' event that is ABOUT something needs a verb to ask which. `qt.selected`
' answers which table or tree row, `qt.gutter_click` which line, `qt.wheel`
' which way, and `qt.which_tab` which tab. Their observation was that "a reader
' coming to this backend fresh would find that rule easier to apply than to
' rediscover per widget".
'
' A TAB IS THE ONE CASE Qt DOES NOT ANSWER BY ITSELF, which is why it needs a
' verb where a row does not. MEASURED: a right-press MOVES a table's or a
' tree's current row, so `qt.selected` already says which was clicked -- and a
' right-press on a QTabBar correctly does NOT move the current index, so
' `currentIndex` answers the tab that was ALREADY ACTIVE. STU-21's rule is the
' opposite: "right-clicking a background tab to close it must not first switch
' you to it". So without this a port acts on the wrong tab in SILENCE, which is
' this backend's named failure class.
'
' THE TABS ARE AT THE BOTTOM ON PURPOSE AND IT IS THE LOAD-BEARING DETAIL. The
' point a context-menu signal carries is in the EMITTING WIDGET'S coordinates
' and the bar is not at its origin, so `tabAt` has to be asked of a MAPPED
' point. Measured with `tabPosition: South`, an unmapped `tabAt` answers -1 --
' "no tab" for a click squarely on one -- while with tabs at the TOP the bar IS
' at the origin and the two agree. Comfortable test data would have hidden the
' mapping entirely, so both positions are exercised and the bottom one is the
' one that can fail.
'
' SELF-CHECKING NOT GOLDEN AND FORCED: every defect here is an ordinary run.
' A missing mapping answers `unknown` for a real tab, which reads exactly like
' a right-click on the page beside it; an index taken from `currentIndex`
' answers a plausible number about the wrong tab; and a tree aim that silently
' refused would leave the handler unfired with nothing said.

function check(what, got, want)
    g.checks = g.checks + 1
    if string(got) = string(want) then
        print "ok   " + what
    else
        g.bad = g.bad + 1
        print "MISMATCH " + what + ": got " + string(got) + ", want " + string(want)
    end if
    return nothing
end function

' STUDIO'S OWN SHELL IN MINIATURE: a file tree on the left, a notebook of open
' documents on the right, both with a right-click menu.
window app( title: "Which", width: 700, height: 420 )
    popup tab_actions
        item close_tab( label: "Close" )
        item copy_path( label: "Copy file path" )
    end popup

    popup file_actions
        item open_file( label: "Open" )
    end popup

    split across
        tree files to show rows
            field depth( depth: true )
            field name( label: "File" )
        end tree

        column
            check hold( label: "On hold" )

            tabs book
            tab "One"
                memo first( plainText: "first document" )
            end tab
            tab "Two"
                memo second( plainText: "second document" )
            end tab
            tab "Three"
                memo third( plainText: "third document" )
            end tab
            end tabs
        end column
    end split

    on book.customContextMenuRequested
        g.tab_menu = g.tab_menu + 1
        g.from_menu = qt.which_tab()
        shown = qt.popup(app, "tab_actions")
    end on

    on book.currentChanged
        g.changed = g.changed + 1
        g.from_changed = qt.which_tab()
    end on

    ' A SIGNAL CARRYING A bool, WHICH IS THE HAZARD THE SLOT CHOICE GUARDS
    ' AGAINST. QMetaObject::connect by index does NOT check argument types --
    ' it hands the slot a void* to reinterpret -- so `toggled(bool)` reaching
    ' `fireIndex(int)` would read four bytes out of one. The slot is therefore
    ' chosen by the type NAME and not by the parameter count, and this handler
    ' is what exercises it: the valgrind tier is where that read would show up,
    ' since the wrong slot answers plausibly and crashes nothing.
    on hold.toggled
        g.toggled = g.toggled + 1
        g.tab_in_toggle = qt.which_tab()
    end on

    ' AND A SIGNAL CARRYING AN int FROM SOMETHING THAT IS NOT A TAB WIDGET,
    ' which is the over-correction guard: the int slot is connected to ANY
    ' single-int signal (one rule rather than a list of names a new Qt version
    ' falls off), so what is narrowed is what it RECORDS. Without that
    ' narrowing `qt.which_tab()` here would answer a check box's state -- a
    ' plausible tab index about something else entirely.
    on hold.stateChanged
        g.state_changed = g.state_changed + 1
        g.tab_in_state = qt.which_tab()
    end on

    on files.customContextMenuRequested
        g.file_menu = g.file_menu + 1
        g.row = qt.selected(app, "files")
        shown2 = qt.popup(app, "file_actions")
    end on
end window

program main()
    rows = [ { depth: 0, name: "src" },
             { depth: 1, name: "eval.c" },
             { depth: 1, name: "qt.c" },
             { depth: 0, name: "README.md" } ]

    if not qt.can_display() then
        print "NODISPLAY"
        return
    end if

    g = { checks: 0, bad: 0, tab_menu: 0, from_menu: 0, changed: 0, from_changed: 0,
          file_menu: 0, row: 0, toggled: 0, tab_in_toggle: 0,
          state_changed: 0, tab_in_state: 0 }

    opened = qt.open(app)
    check("the window opened", opened, true)

    ' -------------------------------------------------- OUTSIDE A HANDLER
    ' A DIFFERENT CLAIM FROM "TAB 0", which is why the answer is `unknown`: 0
    ' is a valid-looking index and would be indistinguishable from the first
    ' tab having been clicked.
    check("outside a handler no tab is being asked about",
          is_unknown(qt.which_tab()), true)

    ' -------------------------------------------------- TABS AT THE BOTTOM
    ok = qt.set(app, "book", "tabPosition", "South")
    check("the tabs moved to the bottom", qt.read(app, "book", "tabPosition"),
          "South")
    sel = qt.set(app, "book", "currentIndex", 0)
    check("the first tab is current", qt.read(app, "book", "currentIndex"), 0)
    check("and the change was reported with its own index", g.from_changed, 0)

    before = g.tab_menu
    clicked = qt.right_click(app, "book", 2)
    check("right-clicking the third tab fired the handler",
          g.tab_menu, before + 1)
    check("and the handler was told which tab", g.from_menu, 2)
    ' THE HALF THAT MAKES THE VERB NECESSARY, and the measured Qt behaviour:
    ' the right-press did NOT switch to the tab it landed on, so a port reading
    ' `currentIndex` would act on tab 0.
    check("and the right-click did not switch to it",
          qt.read(app, "book", "currentIndex"), 0)

    ' -------------------------------------------------- TABS AT THE TOP
    ' The same claim where the bar IS at the origin, so an unmapped `tabAt`
    ' would agree. Kept as the pair, not as the proof.
    ok2 = qt.set(app, "book", "tabPosition", "North")
    before2 = g.tab_menu
    clicked2 = qt.right_click(app, "book", 1)
    check("with the tabs on top it still answers", g.from_menu, 1)
    check("and the handler ran once", g.tab_menu, before2 + 1)

    ' -------------------------------------------------- NOT OVER A TAB
    ' A right-click on the PAGE emits the same signal, and no tab is what it is
    ' about. An ordinary case rather than a corner: it is where the text is.
    before3 = g.tab_menu
    clicked3 = qt.right_click(app, "book")
    check("a right-click on the page still fires the handler",
          g.tab_menu, before3 + 1)
    check("and no tab was under it", is_unknown(g.from_menu), true)

    ' -------------------------------------------------- THE int ROUTE
    ' `tabCloseRequested(int)` carries an index a zero-argument handler cannot
    ' see, which is the other half of the same problem and what makes
    ' `tabsClosable: true` usable. IT REACHES THE SAME SLOT BY THE SAME RULE --
    ' any single-int signal a tab widget emits -- so what is asserted here is
    ' that rule, through `currentChanged`, which a program CAN drive. Driving
    ' the close button itself would need the QTabBar's own child widget, which
    ' gBASIC cannot name; that is stated rather than implied.
    closable = qt.set(app, "book", "tabsClosable", true)
    check("a tab can carry a close button",
          qt.read(app, "book", "tabsClosable"), true)
    before4 = g.changed
    moved = qt.set(app, "book", "currentIndex", 2)
    check("changing tabs reported the change", g.changed, before4 + 1)
    check("and said which tab it was about", g.from_changed, 2)

    ' -------------------------------------------------- A TREE ROW
    ' §0.20 justified `qt.right_click`'s row argument with "right-click the
    ' third file", and the browser that sentence is about was a `table` for one
    ' increment and has been a `tree` since §0.28 -- so the sentence outlived
    ' the widget and the one shape it was written for could not be driven.
    check("the tree filled from its subject",
          qt.cell(app, "files", 3, "name"), "README.md")
    picked = qt.select(app, "files", 0)
    before5 = g.file_menu
    fc = qt.right_click(app, "files", 2)
    check("right-clicking a tree row fired the handler",
          g.file_menu, before5 + 1)
    ' AND THE ROW CAME FREE, which is why a tree needs no verb of its own: Qt
    ' moves the current item on a right-press, so `qt.selected` answers.
    check("and the handler was told which row", g.row, 2)
    check("which is a CHILD row, so the index is the walk and not the top level",
          qt.depth(app, "files", g.row), 1)

    ' -------------------------------------------------- REFUSALS
    on error goto next
    bad1 = qt.right_click(app, "book", 9)
    if error then
        check("a tab that is not there is refused", error.message,
              "qt.right_click: that has no such tab")
    else
        print "MISMATCH tab 9 was clicked"
    end if

    on error goto next
    bad2 = qt.right_click(app, "files", 9)
    if error then
        check("a tree row that is not there is refused", error.message,
              "qt.right_click: that tree has no such row")
    else
        print "MISMATCH row 9 was clicked"
    end if

    on error goto next
    bad3 = qt.right_click(app, "first", 0)
    if error then
        check("a part with no rows or tabs says so", error.message,
              "qt.right_click: `first` is not a table, a tree or a `tabs`, " +
              "so it has no row or tab to click")
    else
        print "MISMATCH a memo answered a row click"
    end if

    ' AND THE CONTROL: the aim still works after the refusals, or "it refuses"
    ' would be satisfied by a verb that refuses everything.
    before6 = g.file_menu
    fc2 = qt.right_click(app, "files", 0)
    check("a refusal does not break the aim", g.file_menu, before6 + 1)
    check("and the first row is a parent", qt.depth(app, "files", g.row), 0)

    ' -------------------------------------------------- A bool IS NOT AN INDEX
    before7 = g.toggled
    flipped = qt.invoke(app, "hold", "toggle")
    check("a bool-carrying signal still reaches its handler",
          g.toggled, before7 + 1)
    check("and no tab was recorded from it", is_unknown(g.tab_in_toggle), true)
    check("an int-carrying signal from a check box also reached its handler",
          g.state_changed, 1)
    check("and a check box's state is not a tab index",
          is_unknown(g.tab_in_state), true)

    shut = qt.close(app)
    check("the window closed", shut, true)

    print "ok   " + string(g.checks) + " checks, " + string(g.bad) + " mismatches"
end program
