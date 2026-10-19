' DOES CLOSING THE LAST WINDOW END `qt.run()`? (§0.17)
'
' `quitOnLastWindowClosed` IS QT'S DEFAULT AND WE NEVER SET IT, so this is a
' measurement of a behaviour gBASIC INHERITS rather than one it implements --
' which is exactly why it needs asserting: nothing in this tree chose it, so
' nothing in this tree would notice it changing, and a desktop application
' whose loop does not end when its last window shuts is a process the person
' cannot get rid of.
'
' BOTH HALVES, because either alone proves nothing. "The loop ended" is
' satisfied by a build where `qt.run()` returns immediately; "the loop kept
' running" is satisfied by one where closing does nothing at all. So: closing
' ONE OF TWO must leave it running, and closing THE LAST must end it.
'
' DRIVEN BY A TIMER, which is §5.1's measurement being relied on rather than
' restated: gBASIC's own sources are serviced while Qt owns the wait, so a
' `timer` fires inside `qt.run()` and can close a window the way a person
' would. BOUNDED, because a loop that never ends is a HANG and a hang is not a
' failure -- the tick counter gives up and says so.

window first( title: "First", width: 240, height: 120 )
    column( margin: 8 )
        field who( label: "Who" )
    end column
end window

window other( title: "Second", width: 200, height: 100 )
    column
        field note( label: "Note" )
    end column
end window

program main(args)
    g = { ticks: 0, log: "" }

    if not qt.can_display() then
        print "SKIP no display"
        return
    end if

    o1 = qt.open(first)
    o2 = qt.open(other)
    print "TWO OPEN"

    beat = timer.every(0.05)
    watch(timer.ticks)
        while count(timer.ticks) > 0
            ev = take_first(timer.ticks)
            g.ticks = g.ticks + 1
            if g.ticks = 2 then
                print "CLOSING ONE OF TWO"
                x = qt.close(other)
            end if
            if g.ticks = 6 then
                ' REACHING THIS AT ALL is the control: four ticks after one of
                ' two windows closed, the loop is demonstrably still running.
                print "STILL RUNNING WITH ONE OPEN"
                print "CLOSING THE LAST ONE"
                x = qt.close(first)
            end if
            if g.ticks > 20 then
                print "LOOP DID NOT END"
                qt.stop()
            end if
        end while
    end watch

    rc = qt.run()
    print "RUN RETURNED " + string(rc)
    print "TICKS " + string(g.ticks)
    print "FIRST OPEN " + string(qt.is_open(first))
    print "OTHER OPEN " + string(qt.is_open(other))
    cancelled = timer.cancel(beat)
end program
