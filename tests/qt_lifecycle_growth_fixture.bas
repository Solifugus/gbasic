' OPEN AND CLOSE A WINDOW MANY TIMES (§0.17).
'
' A NEW SHAPE RATHER THAN A FASTER OLD ONE: before this increment a second
' `qt.open` met "window `app` is already open", so repeated open/close could
' not be written at all -- and the obvious implementation grows for ever,
' because every part of every window leaves a node behind.
'
' HALF BY THE VERB AND HALF BY THE EVENT, deliberately: `qt.close` can delete
' the widget where it stands, while a close noticed in the event filter cannot
' (the filter runs inside that widget's own dispatch and `QWidget::close()`
' goes on to hide it afterwards), so that one waits and is deleted at the next
' open. Two paths, two retirement moments, and a fixture exercising only one
' would leave the other unmeasured.

window app( title: "Growth", width: 200, height: 170 )
    column( margin: 6 )
        field  a( label: "a" )
        field  b( label: "b" )
        field  c( label: "c" )
        check  d( label: "d" )
        button e( label: "e" )
    end column
end window

program main(args)
    if not qt.can_display() then
        print "SKIP no display"
        return
    end if
    n = number(args[0])
    i = 0
    while i < n
        o = qt.open(app)
        if mod(i, 2) = 0 then
            x = qt.close(app)
        else
            x = qt.invoke(app, "app", "close")
        end if
        i = i + 1
    end while
    print "CYCLES " + string(n)
end program
