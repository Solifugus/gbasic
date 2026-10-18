' ASKING BEFORE ACTING. A statement and a question, which is most of what a
' real application needs from a dialog.
'
' `qt.confirm` ANSWERS A BOOLEAN rather than a button code: two buttons is the
' whole of what it offers, and anything richer is a window shown modally, which
' is a different thing with a layout of its own. NO IS THE DEFAULT BUTTON,
' because a question answered by pressing Return without reading should not be
' the destructive one.
'
' A DIALOG IS MODAL TO THE WHOLE PROGRAM, and that is worth knowing before you
' build on it. Qt's loop survives the nested one, but gBASIC cannot re-enter
' while the interpreter is suspended inside its own handler -- so a dialog
' raised FROM A HANDLER (somebody clicked, and the program asks) suspends a
' `timer`, a webserver and an actor mailbox until it is answered. A dialog
' raised OUTSIDE a handler leaves them running, which is why the timer below
' can answer this one.
window app( title: "Asking first", width: 360, height: 170 )
    column( margin: 10, spacing: 8 )
        text state( label: "nothing has happened" )
        field who(  label: "Customer" )
    end column
end window

program main(args)
    g = { want: true, asked: 0 }

    if not qt.can_display() then
        print "no display"
        return
    end if
    qt.open(app)
    qt.write(app, "who", "Ada Lovelace")

    ' A RECIPE HAS NOBODY TO CLICK, so a timer answers for one. In a real
    ' program this is how a dialog TIMES OUT -- and `qt.answer` refuses when
    ' nothing is open rather than doing nothing, because a program left
    ' waiting for somebody who is not there is a hang and not a failure.
    beat = timer.every(0.05)
    watch(timer.ticks)
        while count(timer.ticks) > 0
            ev = take_first(timer.ticks)
            ok = qt.answer(g.want)
        end while
    end watch

    ' THE QUESTION. Guarding a destructive act is the commonest use there is.
    g.want = false
    if qt.confirm(app, "Delete " + qt.read(app, "who") + "?") then
        qt.write(app, "state", "deleted")
    else
        qt.write(app, "state", "kept")
    end if
    print "answered no  -> " + qt.read(app, "state")

    g.want = true
    if qt.confirm(app, "Delete " + qt.read(app, "who") + "?") then
        qt.write(app, "state", "deleted")
    else
        qt.write(app, "state", "kept")
    end if
    print "answered yes -> " + qt.read(app, "state")

    ' THE STATEMENT, which has one button and so is answered either way.
    told = qt.inform(app, "Customer deleted.")
    print "inform returned " + string(told)

    cancelled = timer.cancel(beat)
    qt.stop()
end program
