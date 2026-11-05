' net UNDER QT, WITH A MODAL DIALOG UP (docs/networking_design.md P1). Run by
' tests/run_net.sh under QT_QPA_PLATFORM=offscreen.
'
'   gbasic qt_modal.bas READY_FILE GO_FILE
'
' A watched echo listener, then a confirm raised from `main` -- the arrangement
' in which gBASIC's sources keep running (docs/qt_gui_design.md §0.15; from
' inside a watcher body they are suspended, a documented limit this does not
' test). The runner's Python client connects while the dialog is up and must
' get its echo then. A timer answers the dialog once the runner writes
' GO_FILE, so the echo cannot have happened after the dialog closed.
load net

window app( title: "Net", width: 240, height: 120 )
    column( margin: 8 )
        text t( label: "serving" )
    end column
end window

program main(args)
    G = { go: args[1], answered: false }
    if not qt.can_display() then
        print "NO DISPLAY"
        return
    end if
    qt.open(app)
    watch(net.events)
        while count(net.events) > 0
            e = take_first(net.events)
            if e.kind = "data" then
                line = e.connection.take_line()
                while not is_unknown(line)
                    print "ECHO " + line
                    e.connection.write("echo:" + line + chr(10))
                    line = e.connection.take_line()
                end while
            end if
        end while
    end watch
    beat = timer.every(0.05)
    watch(timer.ticks)
        while count(timer.ticks) > 0
            ev = take_first(timer.ticks)
            if not G.answered and exists({file}G.go) then
                G.answered = true
                ok = qt.answer(true)
            end if
        end while
    end watch
    srv = net.listen({ port: 0 })
    ready {file}= args[0]
    write(ready, string(srv.port))
    print "DIALOG UP"
    answered = qt.confirm(app, "Serving while this is open?")
    print "DIALOG DOWN " + string(answered)
    srv.close()
    qt.close(app)
    stopped = timer.cancel(beat)
end program
