' HOW FAR A MODAL DIALOG REACHES -- the fixture behind run_qt_dialog.sh's
' load-bearing tier, driven from outside because the claim is about what
' ANOTHER PROCESS can see.
'
' It serves one route, then raises a confirm FROM A BUTTON HANDLER, which is
' the normal shape: somebody clicked, and the program asks before acting. The
' suite curls the route before and during. Nothing in here can answer the
' dialog -- that is the point being measured.
server probe( port: 0 )
    get "/ping"( req )
        return { body: "pong" }
    end get
end server

window app( title: "Modal extent", width: 320, height: 140 )
    column( margin: 10 )
        button ask( text: "Ask" )
    end column
    on ask.clicked
        print "DIALOG UP"
        g.answered = qt.confirm(app, "Really?")
        print "DIALOG DOWN"
        return nothing
    end on
end window

program main(args)
    g = { answered: false }
    if not qt.can_display() then
        print "NO DISPLAY"
        return
    end if
    sv = web.serve(probe)
    print "PORT " + string(sv.port)
    qt.open(app)

    ' CLICKED FROM A TIMER so the dialog goes up while `qt.run()` holds the
    ' thread -- the real arrangement, not a program calling confirm from main.
    ' The suite waits for "DIALOG UP" before it asks anything.
    once = timer.after(0.4)
    watch(timer.ticks)
        while count(timer.ticks) > 0
            ev = take_first(timer.ticks)
            qt.invoke(app, "ask", "click")
        end while
    end watch

    rc = qt.run()
end program
