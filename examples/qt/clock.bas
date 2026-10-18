' QT OWNS THE THREAD, AND gBASIC IS STILL SERVICED -- the one architectural
' claim §5.1 of docs/qt_gui_design.md measured before any Qt was written, shown
' here as something you can watch.
'
' Run it:
'     ./gbasic examples/qt/clock.bas
'     QT_QPA_PLATFORM=offscreen ./gbasic examples/qt/clock.bas check
'
' THE QUESTION WAS WHOSE LOOP WAITS. gBASIC's own is one `poll()` over the
' webserver's descriptors, libcurl's transfers, timers and the actor inbox; Qt
' has a loop of its own which must be the one waiting, because it watches the
' keyboard and the mouse. Two arrangements were measured and one was ruled out:
' pumping Qt from gBASIC's loop works perfectly until something opens a modal
' dialog, and then gBASIC's poll NEVER RUNS for the dialog's whole life -- a
' webserver stops answering and every timer misses while a message box is up.
'
' SO QT OWNS THE LOOP AND A gBASIC SOURCE IS REGISTERED WITH IT. This program's
' `timer` keeps firing after `qt.run()` has taken the thread, and what fires is
' ordinary gBASIC. Nothing here arranges it: `qt.run()` is one call and the
' timer is the same `timer.every` any other program uses.
'
' TWO THINGS THIS EXAMPLE EXISTS TO TEACH, both learned by getting them wrong.
'
' THE `watch` GOES INSIDE `program`. At the top level beside a `program` block
' it REGISTERS NOTHING and warning 2106 says so -- the block is what executes.
' And it works inside because `qt.run()` BLOCKS THERE, so `main`'s frame is
' still alive while the loop runs and the watcher can reach the counter in it.
' (A watcher that fires after `main` has returned has no such frame, which is
' why state for one of those has to live elsewhere.)
'
' `qt.stop()` DOES NOT CANCEL YOUR TIMERS, and it should not: a program may
' well close a window and go on serving. But a REPEATING timer keeps the event
' loop alive by itself, so stopping the window and leaving one live hands the
' thread to gBASIC's own loop and the program NEVER EXITS -- measured, 197 more
' ticks before a `timeout` killed it. Cancel what you started.

window clock( title: "Clock", width: 360, height: 240 )
    column( margin: 12, spacing: 8 )
        text now(   label: "--:--:--" )
        text ticks( label: "0 ticks" )
        memo log(   label: "" )
        row
            stretch()
            button close( text: "Close" )
        end row
    end column

    on close.clicked
        qt.stop()
    end on
end window

program main(args)
    if not qt.can_display() then
        print "No display is available, so there is no window to open."
        print "Set QT_QPA_PLATFORM=offscreen to render without one."
        return
    end if

    ' A `check` run ends itself so it can be gated; an ordinary run goes until
    ' somebody closes the window.
    stop_after = 0
    if contains(args, "check") then
        stop_after = 3
    end if
    seen = 0

    qt.open(clock)
    beat = timer.every(0.2)

    watch(timer.ticks)
        while count(timer.ticks) > 0
            ev = take_first(timer.ticks)
            seen = seen + 1
            ' A gBASIC HANDLER WRITING INTO A Qt WIDGET, from inside Qt's own
            ' loop. Three different parts, one of them a `memo` that grows.
            ' A datetime's DOT FIELDS, which is how gBASIC says a clock time.
            tick_at = now()
            qt.write(clock, "now", string(tick_at.hour) + ":" +
                     string(tick_at.minute) + ":" + string(tick_at.second))
            qt.write(clock, "ticks", string(seen) + " ticks")
            qt.write(clock, "log", qt.read(clock, "log") + "tick " +
                     string(seen) + chr(10))
            if stop_after > 0 and seen >= stop_after then
                print "ticks serviced while Qt held the thread: " + string(seen)
                print("the window reads back:                   " +
                      qt.read(clock, "ticks"))
                print("and the memo grew to lines:              " +
                      string(len(split(qt.read(clock, "log"), chr(10))) - 1))
                ' BOTH, AND IN THIS ORDER. Cancelling first would be fine too;
                ' leaving the timer live is what does not work.
                cancelled = timer.cancel(beat)
                qt.stop()
            end if
        end while
    end watch

    ' AFTER THIS LINE QT HAS THE THREAD. Everything the watcher does happens
    ' while it does.
    qt.run()

    ' AND A REPEATING TIMER MUST BE GONE BY NOW or the program will not end:
    ' gBASIC's own loop runs after `main` for as long as there is work.
    if contains(args, "check") then
        print "the loop ended, after " + string(seen) + " ticks"
    else
        done = timer.cancel(beat)
    end if
end program
