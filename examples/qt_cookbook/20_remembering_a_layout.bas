' REMEMBERING A LAYOUT -- reading and setting the pane sizes of a `split`, which
' is what "put my windows back where I left them" is made of.
'
' A SPLIT CANNOT BE NAMED. Its one slot carries the direction (`across` or
' `down`), because a container takes a name OR options and never both -- so
' there is nothing to look up, and a split is addressed by naming a part INSIDE
' it. That is the same rule `qt.pane` already follows, so the sizes are simply
' part of what `qt.pane` answers.
'
' WHAT YOU SET IS A RATIO, NOT A MEASUREMENT. Qt adds the drag handle and the
' layout's margins, and it re-proportions everything when the window is
' resized -- so asking for 200 and 700 gives you something near that ratio and
' NOT those two numbers back. Store them and set them again and the layout
' looks the same, which is what anybody actually wants; compare them for
' equality and you will be disappointed.
'
' A WRONG COUNT IS REFUSED, which is worth knowing because Qt's own behaviour
' is silent: one size on a two-pane split COLLAPSES the second pane, and three
' sizes on a two-pane split honour none of them. Both leave a perfectly
' ordinary-looking layout that is not the one you asked for.
'
' ZERO IS LEGAL, and is how a panel is hidden without being forgotten.

window app( title: "Layout", width: 900, height: 420 )
    split across
        scroll browser
            column( margin: 4 )
                text f1( label: "one.bas" )
                text f2( label: "two.bas" )
            end column
        end scroll
        split down
            source code( plainText: "x = 1" )
            table console to show rows
                field line( label: "Line" )
            end table
        end split
    end split
end window

program main(args)
    if not qt.can_display() then
        print "no display, so no window -- the declaration is still checked"
        return
    end if

    rows = [{ line: "ready" }]
    qt.open(app)

    ' WHAT THE SPLIT LOOKS LIKE NOW. `browser` is a part of the outer split, so
    ' naming it is how the outer split is addressed.
    outer = qt.pane(app, "browser")
    print "the outer split is " + outer.direction + " with " +
          string(outer.panes) + " panes"
    print "the browser is pane " + string(outer.index)

    ' SET IT. Think of these as a ratio: a narrow browser beside a wide editor.
    qt.resize_panes(app, "browser", [200, 700])
    now = qt.pane(app, "browser").sizes
    print "browser narrower than the rest: " + string(now[0] < now[1])

    ' THE INNER SPLIT IS A DIFFERENT SPLIT, addressed by a part of its own.
    qt.resize_panes(app, "code", [300, 100])
    inner = qt.pane(app, "code").sizes
    print "editor taller than the console: " + string(inner[0] > inner[1])
    ' AND THE OUTER ONE DID NOT MOVE.
    still = qt.pane(app, "browser").sizes
    print "the outer split stayed put:     " + string(still[0] = now[0])

    ' REMEMBER AND RESTORE, which is the whole point. In a real program these
    ' go into `persist` with the rest of the session.
    saved = { browser: qt.pane(app, "browser").sizes,
              editor:  qt.pane(app, "code").sizes }
    qt.resize_panes(app, "browser", [450, 450])
    print "after a change, even:           " +
          string(qt.pane(app, "browser").sizes[0] = qt.pane(app, "browser").sizes[1])
    qt.resize_panes(app, "browser", saved.browser)
    back = qt.pane(app, "browser").sizes
    print "and restored to narrow again:   " + string(back[0] < back[1])

    ' HIDING A PANEL: zero, and it comes back.
    qt.resize_panes(app, "browser", [0, 900])
    print "browser hidden:                 " +
          string(qt.pane(app, "browser").sizes[0] = 0)
    qt.resize_panes(app, "browser", saved.browser)
    print "and shown again:                " +
          string(qt.pane(app, "browser").sizes[0] > 0)

    ' A WRONG COUNT IS REFUSED, naming both numbers.
    on error goto next
    qt.resize_panes(app, "browser", [500])
    print "one size for two panes:         " + error.message
    error.clear()
    on error stop

    qt.close(app)
end program
