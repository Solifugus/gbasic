' QT OWNS THE THREAD AND gBASIC IS STILL SERVICED. This is the only recipe
' that calls `ui.run()`, and it is the architectural claim the whole backend
' rests on -- so it is worth watching rather than taking on trust.
'
' WHY QT'S LOOP IS THE ONE THAT WAITS: Qt opens NESTED loops -- a modal dialog,
' a menu, a drag -- and under the other arrangement gBASIC's own poll never
' runs for the dialog's whole life. Measured during the design: a 651ms gap
' over a 600ms dialog, which is a webserver answering nothing. So gBASIC's
' sources ride a Qt timer, and a `timer`, a webserver and an actor mailbox all
' keep running while `ui.run()` holds the thread.
'
' NOTHING HERE ARRANGES IT. `ui.run()` is one call and `timer.every` is the
' same verb any other program uses.
window clock( title: "Still ticking", width: 320, height: 180 )
    column( margin: 10, spacing: 6 )
        text ticks( label: "0 ticks" )
        memo log(   label: "" )
    end column
end window

program main(args)
    if not ui.can_display() then
        print "no display"
        return
    end if

    seen = 0
    ui.open(clock)
    beat = timer.every(0.1)

    ' THE `watch` GOES INSIDE `program`. At the top level beside a `program`
    ' block it registers NOTHING and says nothing about it (warning 2106), and
    ' it works here because `ui.run()` BLOCKS in this frame -- so main's frame
    ' is still alive and the watcher can reach `seen` in it.
    watch(timer.ticks)
        while count(timer.ticks) > 0
            ev = take_first(timer.ticks)
            seen = seen + 1
            ' ORDINARY gBASIC WRITING INTO A Qt WIDGET, from inside Qt's loop.
            clock.write("ticks", string(seen) + " ticks")
            clock.write("log",
                     clock.read("log") + "tick " + string(seen) + chr(10))
            if seen >= 3 then
                ' BOTH, AND THE CANCEL MATTERS: `ui.stop()` DOES NOT CANCEL
                ' YOUR TIMERS, and it should not -- a program may have work
                ' left after its window closes. But leave a repeating timer
                ' live and gBASIC's own post-main loop keeps running, so the
                ' program never exits. Measured during the design at 197
                ' further ticks before a timeout killed it.
                cancelled = timer.cancel(beat)
                ui.stop()
            end if
        end while
    end watch

    ' AND IT RETURNS, which is the other half: `ui.run()` gives the thread
    ' back when something calls `ui.stop()`, so a program carries on
    ' afterwards.
    rc = ui.run()
    print "ticks serviced while Qt held the thread: " + string(seen)
    print "the window reads back:                   " + clock.read("ticks")
    print("and the memo grew to lines:              " +
          string(len(split(clock.read("log"), chr(10))) - 1))
    print "ui.run returned, so the program continues"
end program
