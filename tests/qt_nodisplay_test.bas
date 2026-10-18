' NO DISPLAY IS A SENTENCE, NEVER AN ABORT (docs/qt_gui_design.md §0.3).
'
' The two things this fixture exists to show are both about SURVIVAL rather
' than about the message: the line printed BEFORE the attempt must still
' arrive -- a crash takes block-buffered stdout with it, so a program tracing
' its progress would print nothing and the failure would read as line 1 -- and
' the refusal must be CATCHABLE, so a program can degrade to a text interface
' instead of ending.
window tiny( title: "Tiny" )
    column
        button go( text: "Go" )
    end column
end window

program main()
    print "progress line one"
    print "progress line two"

    on error goto next
    ok = qt.open(tiny)
    if error then
        print "REFUSED: " + error.message
    else
        print "MISMATCH a window opened with no display"
    end if

    ' AND THE PROGRAM IS STILL RUNNING, which is the half a crash takes away.
    print "still here, and can say so"
end program
