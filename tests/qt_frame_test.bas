' THE WINDOW'S OWN FURNITURE (§0.30) -- dock panes, a toolbar and a status bar,
' under QT_QPA_PLATFORM=offscreen.
'
' THE FINDING THAT MADE THIS ONE INCREMENT RATHER THAN THREE: the three asks
' left in docs/qt_studio_requests.md after §0.28 and §0.29 are dock panes (§3),
' a status bar and a toolbar (§4) -- and every one of them is a QMainWindow
' feature. So the question was §0.18's, asked again of a different root: does
' everything this backend builds work inside one? Measured yes -- a form, a
' table, tabs, a splitter, a `source` and a menu bar all do -- so nothing below
' the root had to change.
'
' ONLY WHEN THE DECLARATION ASKS, which is §0.18's own rule for the modal root:
' making every window a QMainWindow would have worked and been a wider change
' for nothing. A window asks by holding a `docks`, a `toolbar` or a `status`,
' and a plain QWidget root genuinely HAS no dock areas -- so a `dock` on an
' ordinary window is a refusal rather than a policy.
'
' WHAT A CHECK COULD OBSERVE IF THIS DID NOTHING: a pane put in the WRONG AREA
' is invisible from the language. Every part inside it reads back by name
' whichever edge it ended up on, and `visible` is true either way -- so a
' backend that ignored the area entirely would pass every other check here.
' `qt.dock` is the oracle for that, built before this fixture (§0.8's lesson),
' and it asks THE WINDOW rather than anything we remembered, which is `qt.at`'s
' and `qt.pane`'s own justification.

window ide( title: "IDE", width: 900, height: 620 )
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

    ' FIVE PANES DOWN ONE EDGE IS WHAT STUDIO HAS NOW AND IS TRYING TO GET
    ' AWAY FROM, so panes in ONE block are TABBED: *"five panes become five
    ' tabs along one edge, which is what every professional IDE does with
    ' exactly this content."*
    docks right
        dock branches "Branches"
            text b( label: "main" )
        end dock
        dock results "Results"
            memo out()
        end dock
    end docks

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

' A WINDOW THAT ASKS FOR NO FURNITURE MUST BE UNTOUCHED -- the control without
' which "a QMainWindow works" is satisfied by making every root one.
window plain( title: "Plain", width: 320, height: 200 )
    column
        field who( label: "Name" )
        button go( label: "Go" )
    end column
end window

program main()
    g = { checks: 0, bad: 0 }

    if not qt.can_display() then
        print "NODISPLAY"
        return
    end if

    qt.open(ide)

    ' ---- THE PANES ARE WHERE THE DECLARATION PUT THEM ------------------
    check("a pane is in its block's area", qt.dock(ide, "branches").area,
          "right")
    check("and its sibling in the same one", qt.dock(ide, "results").area,
          "right")
    ' THE SECOND BLOCK IS THE CHECK THAT SEPARATES "the area was read" from
    ' "everything went to one edge" -- without it a backend hardcoding `right`
    ' passes both checks above.
    check("a second block is a different edge", qt.dock(ide, "console").area,
          "bottom")

    ' ---- PANES IN ONE BLOCK ARE TABBED, which is the ask's own point ----
    check("two panes in one block share a tab group",
          string(qt.dock(ide, "branches").tabbed_with), "1")
    check("and a pane alone in its block does not",
          string(qt.dock(ide, "console").tabbed_with), "0")

    ' A NAME THAT IS NOT A DOCK ANSWERS unknown -- a different claim from "a
    ' dock with no area", the distinction `qt.pane` and `principal()` make.
    check("a part that is not a pane is not a dock",
          is_unknown(qt.dock(ide, "code")), true)

    ' ---- THE PANE'S CONTENTS ARE ORDINARY ------------------------------
    ' A dock is given a column, the choice a `tab` and a `scroll` already
    ' make, so anything works inside one with no further vocabulary.
    check("a part inside a pane reads back", qt.read(ide, "b", "text"), "main")
    qt.write(ide, "out", "a line")
    check("and so does a memo", qt.read(ide, "out", "plainText"), "a line")

    ' ---- THE STATUS BAR, AND WHY IT NEEDS NO VERB ----------------------
    ' MEASURED: a QStatusBar has NO property for its text -- `message` and
    ' `currentMessage` are both absent and `showMessage(QString)` takes an
    ' argument `qt.invoke` cannot pass -- while a QLabel in it carries `text`
    ' as an ordinary Q_PROPERTY. So the by-name route already reaches it.
    check("a status bar's text reads back", qt.read(ide, "msg", "text"),
          "Ready")
    qt.set(ide, "msg", "text", "Saved")
    check("and a program can change it", qt.read(ide, "msg", "text"), "Saved")
    check("its neighbour is undisturbed", qt.read(ide, "pos", "text"), "1:1")

    ' ---- THE TOOLBAR'S ENTRIES ARE QActions ----------------------------
    ' Which is why a toolbar needed no new part kind: an `item` means the same
    ' thing in a toolbar as in a menu, by construction -- one walk serves both.
    check("a toolbar is addressed by its name",
          qt.read(ide, "main", "windowTitle"), "main")
    check("an item in it carries its label", qt.read(ide, "open", "text"),
          "Open")
    check("and can be set checkable like a menu's",
          string(qt.set(ide, "save", "checkable", true)), "true")
    check("which reads back", string(qt.read(ide, "save", "checkable")),
          "true")

    ' ---- THE MENU BAR IS STILL THE MENU BAR ----------------------------
    ' MEASURED: a QMenuBar added to the CENTRAL widget's layout renders INSIDE
    ' the central area -- a bar in the middle of the window -- so a main window
    ' must place it with `setMenuBar`. That it is still found by the existing
    ' oracle is what says the two paths agree.
    bars = qt.menus(ide)
    check("the menu bar survived the new root", len(bars), 1)
    check("and it is the one declared", bars[0], "File")
    check("and its items too", qt.read(ide, "quit", "text"), "Quit")

    ' ---- THE SAVED LAYOUT ----------------------------------------------
    ' A ROUND TRIP, not a value: Qt's blob is opaque and a check that pinned
    ' its text would be pinning Qt's format.
    saved = qt.layout(ide)
    check("a layout comes back as text", len(saved) > 0, true)
    qt.set(ide, "branches", "floating", true)
    check("a pane can be floated", string(qt.dock(ide, "branches").floating),
          "true")
    check("restoring the saved layout succeeds",
          string(qt.restore_layout(ide, saved)), "true")
    check("and the pane is back where it was",
          string(qt.dock(ide, "branches").floating), "false")

    ' AN UNRECOGNISED LAYOUT IS REPORTED, NOT RAISED -- which is what a program
    ' finds after its window's declaration has changed, an ordinary thing to
    ' meet on startup.
    check("a layout this window did not save is refused as a value",
          string(qt.restore_layout(ide, "bm90IGEgbGF5b3V0")), "false")

    qt.close(ide)

    ' ---- THE CONTROL: AN ORDINARY WINDOW IS UNTOUCHED ------------------
    qt.open(plain)
    check("a window with no furniture still works",
          string(qt.read(plain, "who", "enabled")), "true")
    on error goto next
    qt.layout(plain)
    check("and has no layout to save", contains(error.message,
          "has a layout to save"), true)
    error.clear()
    ' AND NOTHING IN IT IS A DOCK, answered as an ABSENCE rather than a
    ' refusal -- which this fixture corrected rather than confirmed: a
    ' sentence about the window having no dock areas was unreachable by
    ' construction, a QDockWidget being buildable only on a main window, so
    ' no program could ever have produced it. `unknown` is the honest answer
    ' and the one that composes with `is_unknown`.
    check("and nothing in it is a dock", is_unknown(qt.dock(plain, "who")),
          true)
    qt.close(plain)

    print "ok   " + string(g.checks) + " checks, " + string(g.bad) + " mismatches"
end program

function check(what, got, want)
    g.checks = g.checks + 1
    if got = want then
        print "ok   " + what
    else
        g.bad = g.bad + 1
        print "MISMATCH " + what + ": got " + string(got) + ", want " + string(want)
    end if
    return nothing
end function
