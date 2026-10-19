' `qt.stop()` IS NOT A CLOSE (§0.17).
'
' THIS IS A REGRESSION GUARD ON A DEFECT THIS INCREMENT EXPOSED RATHER THAN
' INTRODUCED. `gb_qt_stop` called `QCoreApplication::quit()`, and MEASURED on
' Qt 6.10.2 that CLOSES EVERY TOP-LEVEL WINDOW -- it calls closeAllWindows --
' where `QCoreApplication::exit(0)` ends the loop and leaves them alone.
'
' SO ASKING FOR THE THREAD BACK WAS DISMISSING THE PROGRAM'S WINDOWS, and
' nothing could see it: the windows were merely HIDDEN, every part stayed
' readable, and no fixture asked whether the window was still up. It became
' visible only once a close was bookkeeping, when a program that reads its
' window after `qt.run()` returned started being told it was not open -- and
' MEASURED rather than grepped, since eight programs in this tree read a window
' textually after `qt.run()` and exactly ONE actually broke.
'
' BOTH HALVES, because each covers a different wrong implementation: `is_open`
' catches a `stop` that fires the close bookkeeping, and `visible` catches one
' that merely hides the window -- which is what the defect actually did, and
' which `is_open` alone would have gone on missing.

window app( title: "Stopping", width: 240, height: 120 )
    column( margin: 8 )
        field who( label: "Who" )
    end column
    on app.closed
        g.closed = g.closed + 1
    end on
end window

program main(args)
    g = { ticks: 0, closed: 0 }

    if not qt.can_display() then
        print "SKIP no display"
        return
    end if

    o = qt.open(app)
    w = qt.write(app, "who", "Ada Lovelace")

    beat = timer.every(0.05)
    watch(timer.ticks)
        while count(timer.ticks) > 0
            ev = take_first(timer.ticks)
            g.ticks = g.ticks + 1
            if g.ticks >= 2 then
                ' ONLY `qt.stop()`. Nothing here closes anything.
                c = timer.cancel(beat)
                qt.stop()
            end if
        end while
    end watch

    rc = qt.run()
    print "RUN RETURNED " + string(rc)
    print "STILL OPEN " + string(qt.is_open(app))
    ' EVERY LINE PRINTS EVEN WHEN THE WINDOW IS GONE. A fixture that dies
    ' halfway through its own report leaves the suite with nothing to say but
    ' the exit code, which is the "a red tier that reports nothing" class --
    ' and here the two reads are exactly what a broken `stop` makes raise.
    on error goto next
    vis = qt.read(app, "app", "visible")
    if error then
        print "STILL VISIBLE unreadable: " + error.message
        error.clear()
    else
        print "STILL VISIBLE " + string(vis)
    end if
    txt = qt.read(app, "who")
    if error then
        print "STILL READS unreadable: " + error.message
        error.clear()
    else
        print "STILL READS " + txt
    end if
    print "CLOSED FIRED " + string(g.closed)
end program
