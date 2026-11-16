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
        g.answered = ui.confirm(app, "Really?")
        print "DIALOG DOWN"
        return nothing
    end on
end window

program main(args)
    g = { answered: false }
    if not ui.can_display() then
        print "NO DISPLAY"
        return
    end if
    sv = web.serve(probe)
    print "PORT " + string(sv.port)
    ui.open(app)

    ' CLICKED FROM A TIMER so the dialog goes up while `ui.run()` holds the
    ' thread -- the real arrangement, not a program calling confirm from main.
    ' It clicks only once the suite has written args[0], the go file, after
    ' its control request: a fixed delay raced that request on a slow machine
    ' (s390x under QEMU), and the control then met a server already stalled.
    ' The suite waits for "DIALOG UP" before it asks anything more.
    go = args[0]
    poll = timer.every(0.1)
    watch(timer.ticks)
        while count(timer.ticks) > 0
            ev = take_first(timer.ticks)
            if exists(go) then
                timer.cancel(poll)
                ui.invoke(app, "ask", "click")
            end if
        end while
    end watch

    rc = ui.run()
end program
