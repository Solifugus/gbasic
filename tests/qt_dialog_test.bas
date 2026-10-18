' DIALOGS (§0.15) -- under QT_QPA_PLATFORM=offscreen: real Qt, real modal
' loops, no display.
'
' SELF-CHECKING NOT GOLDEN AND FORCED: every defect here is an ORDINARY-LOOKING
' RUN. A confirm that always answers `false` is the safe answer by accident
' rather than by decision and looks identical to a careful user; an `answer`
' that silently did nothing would leave a program waiting for a person who is
' not there, which is a HANG rather than a failure.
'
' WHAT MAKES ANY OF THIS TESTABLE is that gBASIC's own sources are serviced
' while the nested loop is up -- so a `timer` can answer the program's own
' dialog. Measured in C++ first (3 of 3 ticks during a modal box, and the tick
' could press its button), then here through the interpreter.
'
' AND THE LIMIT OF THAT WAS MEASURED TOO, because it is narrower than §5.1
' reads: gBASIC cannot re-enter while the interpreter is SUSPENDED INSIDE ITS
' OWN HANDLER. A dialog raised outside a handler leaves the sources running; a
' dialog raised FROM a handler -- the normal case, since somebody clicked --
' suspends them until it is answered, so `qt.answer` cannot reach it and a
' person is the only answer. run_qt_dialog.sh asserts that with a real server
' on a real port, because a limitation nobody can observe is a limitation
' nobody will believe.
'
' NOT ASSERTED, and said so rather than implied: a dialog opened OVER another.
' The backend keeps a stack so a nested one is not refused, but nothing here
' exercises it, and a claim nobody has run is not a claim.

function check(what, got, want)
    if string(got) = string(want) then
        print "ok   " + what
    else
        print "MISMATCH " + what + ": got " + string(got) + ", want " + string(want)
    end if
end function

window app( title: "Dialogs", width: 340, height: 160 )
    column( margin: 10 )
        text shown( label: "" )
        button ask( text: "Ask" )
    end column

    ' A DIALOG FROM A HANDLER, which is the real-world shape: the person
    ' clicked something, and the program asks before doing it.
    on ask.clicked
        g.from_handler = qt.confirm(app, "Really?")
        return nothing
    end on
end window

program main(args)
    g = { want: true, ticks: 0, from_handler: false }

    if not qt.can_display() then
        print "SKIP no display"
        return
    end if
    qt.open(app)

    print "-- answering with nothing open is refused, not ignored"
    ' A SILENT NO-OP WOULD BE THE WORSE FAILURE: a program would wait for a
    ' person who is not there, and a hang is not a failure.
    on error goto nothing_open
    x = qt.answer(true)
    print "MISMATCH qt.answer was accepted with no dialog open"
    return
nothing_open:
    check("refused, naming the state", contains(error.message, "no dialog is open"), true)
    error.clear()

    ' THE TIMER THAT ANSWERS. It only fires because the pump is armed for the
    ' dialog's duration -- which it was NOT when these verbs were first
    ' written, and the first `qt.confirm` outside `qt.run()` hung until a
    ' 30-second timeout.
    beat = timer.every(0.05)
    watch(timer.ticks)
        while count(timer.ticks) > 0
            ev = take_first(timer.ticks)
            g.ticks = g.ticks + 1
            ok = qt.answer(g.want)
        end while
    end watch

    print "-- a question, answered both ways"
    ' ASSERTED AS A DIFFERENCE: either answer alone passes on a confirm that
    ' always says the same thing, and `false` is the DEFAULT button, so a
    ' broken one most likely always says no.
    g.want = true
    check("yes is yes", qt.confirm(app, "Delete this?"), true)
    g.want = false
    check("and no is no", qt.confirm(app, "Delete this?"), false)

    print "-- a statement has one button, so it is answerable either way"
    g.want = false
    check("an inform returns", qt.inform(app, "Saved."), true)
    g.want = true
    check("whichever way it is answered", qt.inform(app, "Saved again."), true)

    print "-- gBASIC was serviced while the boxes were up"
    ' THE ARCHITECTURAL CLAIM, through the interpreter. Without it none of the
    ' above could have happened at all, so this is the premise rather than a
    ' separate feature -- stated because a reader should not have to infer it.
    check("the timer fired during a nested loop", g.ticks > 0, true)

    ' A DIALOG FROM A HANDLER IS NOT EXERCISED HERE, deliberately: nothing in
    ' this process could answer it, so the fixture would hang rather than fail.
    ' The suite asserts that limitation from outside, with a server.

    cancelled = timer.cancel(beat)
    qt.stop()
end program
