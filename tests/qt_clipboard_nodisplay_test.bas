' THERE IS NO CLIPBOARD WITHOUT A DISPLAY, and that is a sentence rather than
' an abort (docs/qt_gui_design.md §0.3 and §0.33).
'
' The clipboard belongs to a QApplication, and constructing one with no display
' calls qFatal AND ABORTS -- uncatchably, taking block-buffered stdout with it,
' so a program tracing its progress prints NOTHING and the failure reads as
' line 1. So both halves matter here and neither is about the wording: the
' refusal must be CATCHABLE so a program can carry on without the clipboard,
' and the lines printed around it must still arrive.
'
' AND IT MUST NAME THE DISPLAY. `qt.copy` takes no window, so there is no
' window to blame -- but `gb_qt_begin`'s own refusal says "no window can be
' opened", which is true of a window and the wrong cause for a clipboard. The
' wrong-cause class this tree has now recorded six times.
program main()
    print "progress line one"

    on error goto next
    ok = qt.copy("something")
    if error then
        print "COPY REFUSED: " + error.message
    else
        print "MISMATCH a copy succeeded with no display"
    end if

    on error goto next
    text = qt.clipboard()
    if error then
        print "READ REFUSED: " + error.message
    else
        print "MISMATCH a read succeeded with no display"
    end if

    ' AND THE PROGRAM IS STILL RUNNING, which is the half a crash takes away.
    print "still here, and can say so"
end program
