' THE most important thing to know before writing a GUI in gBASIC.
'
' gBASIC HAS NO CLOSURES. A handler that assigns to a variable from an
' enclosing scope does not update it -- it silently creates a function-local of
' the same name, and the outer value never changes. GUI code is nothing but
' callbacks, so this is where it bites hardest: a counter that stays at 1, a
' flag that never flips, a loop that never stops.
'
' The fix is to keep state in a RECORD and mutate a field. A record is shared,
' so a handler can change what the rest of the program sees.
'
' The interpreter warns about the broken form (read-then-assign of an outer
' name) exactly once per site -- but only if you READ the variable first, which
' the broken version below does.
load gi
load gtk

gtk.require()
gtk.init()

' RIGHT: a shared record. The field survives every callback.
state = { ticks: 0, done: false }

function tick()
    state.ticks = state.ticks + 1
    print "  tick " + string(state.ticks)
    if state.ticks >= 3 then
        state.done = true
        gi.quit()
        return 0
    end if
    return 1                    ' non-zero keeps the timeout scheduled
end function

print "running a real event loop:"
gi.timeout(10, tick)
gi.main()

print "ticks after the loop : " + string(state.ticks)
print "done flag            : " + string(state.done)
print ""
print "A plain `ticks = ticks + 1` inside that handler would have left"
print "ticks at 0 and the loop would never have quit."
