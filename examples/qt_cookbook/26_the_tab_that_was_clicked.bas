' THE TAB THAT WAS CLICKED, AND THE CLIPBOARD -- a notebook whose right-click
' menu acts on the tab under the pointer, and a `Copy file path` that works.
' Under QT_QPA_PLATFORM=offscreen, so it runs with no screen.
'
' THE TRAP THIS RECIPE IS ABOUT: a right-click on a tab does NOT make it the
' current tab, and that is Qt behaving correctly -- right-clicking a background
' tab to close it must not first switch you to it. So a handler that reads
' `currentIndex` acts on whichever tab was ALREADY ACTIVE, which closes the
' wrong document and raises nothing. `qt.which_tab()` answers the one the event
' is about.
'
' IT ANSWERS `unknown` when the click was not over a tab, which is ordinary
' rather than a corner: a right-click on the PAGE emits the same signal, and
' that is where the text is.

window app( title: "Notebook", width: 520, height: 300 )
    popup tab_menu
        item copy_path( label: "Copy file path" )
        item close_one( label: "Close" )
    end popup

    column
        tabs book
            tab "notes.bas"
                memo one( plainText: "' the first file" )
            end tab
            tab "util.bas"
                memo two( plainText: "' the second file" )
            end tab
            tab "main.bas"
                memo three( plainText: "' the third file" )
            end tab
        end tabs

        text said( label: "right-click a tab" )
    end column

    on book.customContextMenuRequested
        ' ASK WHICH TAB, then decide what the menu may offer for it. A program
        ' that reads `currentIndex` here is reading the wrong tab.
        which = qt.which_tab()
        if is_unknown(which) then
            app.set("said", "text", "that was the page, not a tab")
        else
            g.aimed = which
            app.set("said", "text",
                   "right-clicked " + g.paths[which])
            shown = app.popup("tab_menu")
        end if
    end on

    on copy_path.triggered
        ' AND THE CLIPBOARD TAKES NO WINDOW: there is one per application, so
        ' there is nothing to name. It copies eagerly, so the text is on the
        ' clipboard the moment this returns.
        ok = qt.copy(g.paths[g.aimed])
        app.set("said", "text", "copied " + qt.clipboard())
    end on
end window

program main(args)
    g = { aimed: 0,
          paths: [ "/src/notes.bas", "/src/util.bas", "/src/main.bas" ] }

    if not qt.can_display() then
        print "no display, so no window -- and no clipboard either"
        return
    end if

    qt.open(app)
    app.set("book", "currentIndex", 0)
    print "the first tab is current: " + string(app.read("book", "currentIndex"))

    ' A PERSON WOULD RIGHT-CLICK THE THIRD TAB. `qt.right_click`'s third
    ' argument aims at a tab's own rectangle, through the tab bar, which is
    ' where a real click lands.
    app.right_click("book", 2)
    print "the status line says:   " + app.read("said", "text")
    print "and the current tab is still: " + string(app.read("book", "currentIndex"))

    ' THE MENU IS STILL UP and a visible menu grabs the mouse, so a program
    ' driving itself hides it before doing anything else.
    app.invoke("tab_menu", "hide")

    ' NOW THE MENU ITEM, which is what a person would click next.
    app.invoke("copy_path", "trigger")
    print "after Copy file path:   " + app.read("said", "text")
    print "the clipboard holds:    " + qt.clipboard()

    ' AND A RIGHT-CLICK THAT MISSES EVERY TAB: the page emits the same signal.
    app.right_click("book")
    print "clicking the page:      " + app.read("said", "text")

    app.close()
end program
