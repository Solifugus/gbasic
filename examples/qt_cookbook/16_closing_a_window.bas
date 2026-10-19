' CLOSING A WINDOW -- and knowing when the person closed it for you.
'
' `qt.close(win)` IS THE OTHER HALF OF `qt.open`, and a close is BOOKKEEPING
' rather than a hide: Qt leaves a dismissed window alive and still holding its
' values, so a program that went on reading one would be reading off a screen
' nobody is looking at. After a close, `qt.read`, `qt.write` and `qt.subject`
' all refuse by name and `qt.is_open` answers false.
'
' WHICH MAKES `on <win>.closed` THE PLACE TO SAVE. It fires BEFORE the window
' stops being open, so it is the one moment a program can still read what
' somebody typed -- and it fires however the window closed, including the
' window manager's own button, which is the case you cannot test and your users
' will do every day.
'
' THE WINDOW IS A PART OF ITSELF. Its own name reaches it in the same by-name
' table every part lives in, so `qt.read(app, "app", "windowTitle")`,
' `qt.set(app, "app", ...)` and `qt.invoke(app, "app", "close")` all work with
' no extra vocabulary. The one cost: a part may NOT take the window's name, and
' is refused by name if it tries.
window app( title: "Closing", width: 360, height: 180 )
    column( margin: 10, spacing: 8 )
        form to edit cust
            field who(   label: "Customer" )
            field limit( label: "Limit", type: {USD} )
        end form
    end column

    ' SAVE HERE. `g` is one record whose fields handlers mutate, which is where
    ' GUI state lives in gBASIC -- a handler cannot rebind a variable in `main`
    ' (warning 2104 says so and names this remedy), but it can write a field.
    on app.closed
        g.saved  = qt.subject(app, "cust")
        g.closes = g.closes + 1
    end on
end window

program main(args)
    g = { saved: {}, closes: 0 }

    if not qt.can_display() then
        print "no display -- nothing to open"
        return
    end if

    cust = { who: "Ada Lovelace", limit: {USD}"500.00" }
    qt.open(app)
    print "open:    " + string(qt.is_open(app))

    ' THE WINDOW ITSELF, ADDRESSED BY ITS OWN NAME.
    print "title:   " + qt.read(app, "app", "windowTitle")
    x = qt.set(app, "app", "windowTitle", "Closing (edited)")
    print "renamed: " + qt.read(app, "app", "windowTitle")

    ' Somebody types.
    y = qt.write(app, "who", "Grace Hopper")

    ' CLOSING RUNS THE HANDLER FIRST.
    ok = qt.close(app)
    print "closed:  " + string(qt.is_open(app))
    print "saved:   " + g.saved.who + ", " + string(g.saved.limit)

    ' AND AFTERWARDS THE WINDOW REFUSES, which is the point: there is nothing
    ' on screen to read. `qt.is_open` is how you ask without raising.
    on error goto next
    stale = qt.read(app, "who")
    if error then
        print "after:   " + error.message
        error.clear()
    end if

    ' REOPENING BUILDS A FRESH WINDOW. The form captures its subject again, so
    ' what was typed before the close is gone -- a close is not a hide you can
    ' undo.
    qt.open(app)
    print "reopened who: " + qt.read(app, "who")
    print "reopened title: " + qt.read(app, "app", "windowTitle")

    ' AND `qt.stop()` IS NOT A CLOSE. It means "give me the thread back" and
    ' nothing more: the window stays open, stays readable, and no `closed`
    ' handler fires. (Qt's own `quit()` closes every top-level window; gBASIC
    ' deliberately does not use it.)
    print "closes so far: " + string(g.closes)
    qt.stop()
end program
