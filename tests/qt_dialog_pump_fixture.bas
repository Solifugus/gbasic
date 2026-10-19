' WHY A DIALOG STARVES gBASIC -- and the answer is TWO guards, not one.
'
' §0.15 FIRST SAID gBASIC CANNOT RE-ENTER WHILE THE INTERPRETER IS SUSPENDED
' INSIDE ITS OWN HANDLER. The measurement behind that was right; the wording
' points at a handler, and THERE IS NO HANDLER ANYWHERE IN THIS FIXTURE and the
' stall is identical. So "somebody clicked" is not what matters -- what matters
' is that a WATCHER BODY was on the stack, which is far wider: a `timer` body,
' a webserver handler, an actor mailbox watcher, an `http` event watcher, and a
' button clicked from any of them.
'
' AND TWO INDEPENDENT THINGS HOLD THE STALL, EACH SUFFICIENT ALONE. Measured by
' removing them one at a time, which is the only way to tell:
'
'   QT'S      gBASIC's sources ride ONE reference-counted Qt timer, and Qt
'             will not re-deliver a timer whose handler is on the stack --
'             0 pump ticks against 3 for a fresh timer during the same box.
'   gBASIC'S  `watcher_drain` returns at once while a watcher body is on the
'             stack, so the loop polls, finds work, and declines to dispatch.
'
' REMOVING EITHER ONE CHANGES NOTHING, which is what makes this worth writing
' down rather than guessing at: with the dialog given a timer of its own the
' pump reached gBASIC's loop 397 TIMES during this dialog and the stall did not
' move, and with gBASIC's guard alone removed the pump still never ticked.
' WITH BOTH OFF IT DISSOLVES -- TICK 2 arrives and answers the box.
'
' SO THE FIX IS NOT A TIMER CHANGE, which is the useful half: it needs watcher
' dispatch to become RE-ENTRANT, and that guard is deliberate and is the right
' call -- a watcher body re-entered mid-statement is the hazard, not the
' remedy. A language decision, not this backend's to take.
'
' THIS FIXTURE HANGS BY DESIGN and the suite bounds it. It prints TICK 1, raises
' a confirm, and TICK 2 must never arrive: nothing can answer the box, so the
' program waits for a person who is not there. A hang is not a failure, which is
' why the assertion is made from outside.
window app( title: "Pump", width: 240, height: 120 )
    column( margin: 8 )
        text t( label: "no handler here" )
    end column
end window

program main(args)
    g = { ticks: 0, asked: false }
    if not qt.can_display() then
        print "NO DISPLAY"
        return
    end if
    qt.open(app)
    beat = timer.every(0.1)
    watch(timer.ticks)
        while count(timer.ticks) > 0
            ev = take_first(timer.ticks)
            g.ticks = g.ticks + 1
            print "TICK " + string(g.ticks)
            if not g.asked then
                g.asked = true
                print "DIALOG UP"
                answered = qt.confirm(app, "Really?")
                print "DIALOG DOWN " + string(answered)
            else
                ' ONLY REACHABLE IF WATCHER DISPATCH BECAME RE-ENTRANT. If it
                ' ever is, the limitation is gone and three documents overstate
                ' it -- which is why the suite fails saying so.
                ok = qt.answer(true)
            end if
            if g.ticks > 40 then
                qt.stop()
            end if
        end while
    end watch
    rc = qt.run()
    print "LOOP ENDED"
end program
