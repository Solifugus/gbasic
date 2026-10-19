' HOW FAR A MODAL WINDOW REACHES -- the CONTROL that makes §0.15's limit
' precise, and the reason a modal window is usable at all.
'
' §0.15 measured that a dialog raised FROM A HANDLER suspends gBASIC's own
' sources until it is answered: two guards hold it, each sufficient alone, and
' that is a real limitation stated in §0.15, the reference and the cookbook.
'
' THIS IS THE OTHER CASE AND IT IS THE COMMON ONE. A modal window is opened
' from `main` -- "show this form and wait" is how a program asks for data --
' and there gBASIC's sources KEEP RUNNING: the webserver answers, a timer
' fires, and the program can therefore answer its own window, which is also
' what makes any of this drivable without a person.
'
' WITHOUT THIS CONTROL, §0.15's measurement reads as "a nested Qt loop starves
' gBASIC", which is false and would be the wrong lesson to carry into the port.
' What starves it is re-entering the INTERPRETER, not the loop.
'
' Driven from outside, because the claim is about what another process can see.
server probe( port: 0 )
    get "/ping"( req )
        return { body: "pong" }
    end get
end server

window form( title: "Modal extent", width: 320, height: 160 )
    column( margin: 10 )
        field who( label: "Name" )
        button save( label: "Save" )
    end column
    on save.clicked
        x = qt.accept(form)
    end on
end window

program main(args)
    g = { ticks: 0 }
    if not qt.can_display() then
        print "NO DISPLAY"
        return
    end if
    sv = web.serve(probe)
    print "PORT " + string(sv.port)

    ' THE TIMER IS THE PROOF AS WELL AS THE DRIVER: it can only fire if
    ' gBASIC's sources are being serviced inside the modal loop.
    beat = timer.every(0.2)
    watch(timer.ticks)
        while count(timer.ticks) > 0
            ev = take_first(timer.ticks)
            g.ticks = g.ticks + 1
            print "TICK " + string(g.ticks)
            ' A GATE FILE, NOT A TICK COUNT: the suite creates it once it has
            ' finished asking the server questions, so the window stays up for
            ' exactly as long as the measurement needs and no guessed interval
            ' decides whether the tier measured anything. The pattern
            ' run_process_lifetime.sh and run_web_pool.sh already use.
            if exists({file}args[0]) then
                print "ANSWERING"
                x = qt.accept(form)
            end if
            if g.ticks >= 150 then
                ' AND A BOUND BEHIND IT, because an unanswered modal window is
                ' a HANG and a hang is not a failure.
                print "GIVING UP"
                x = qt.accept(form)
            end if
        end while
    end watch

    print "MODAL UP"
    accepted = qt.open_modal(form)
    print "MODAL DOWN " + string(accepted)
    print "TICKS " + string(g.ticks)
    ' IT ENDS ITSELF. Both sources have to go or gBASIC's post-main loop keeps
    ' running with nothing to do: the timer is cancelled and the listener is
    ' drained (`server.draining = true` stops it accepting and lets the loop
    ' run out of work). A fixture the suite has to kill is one that can be left
    ' behind, which is the leak run_qt_dialog.sh had to be fixed for.
    c = timer.cancel(beat)
    sv.draining = true
    print "DONE"
end program
