' Warning 2107 must NOT fire on a write through a HANDLE held in a loop
' element, and the write must reach the object.
'
' Reported from the gBASIC Studio bench (2026-09-25): filling a form from a
' `for each` over widget rows printed "this writes to `e`, which is a COPY of
' the element ... the write is discarded", and the entry really was filled in.
' The element IS a copy; the handle inside it refers to the same object either
' way, so the write is not discarded and the message was false.
'
' THIS IS THE HALF run_for_each_index.sh CANNOT ASSERT. There, the same
' two-hop shape over a nested RECORD genuinely IS discarded, and the tier pins
' that missed true positive as a COST. Only a reference-kinded value shows the
' silence is CORRECT rather than merely quieter, and the reference kinds all
' live behind an optional dependency -- so the reason lives here, with `gi`.
'
' Gio, not Gtk: a GObject property is a GObject property, and this needs no
' display. Both halves are asserted -- the ids CHANGED (so the write landed)
' and the runner requires stderr to be EMPTY (so nothing warned).
load gi

program main(args)
    gi.require("Gio", "2.0")
    rows = [
        { app: gi.new("Gio.Application", "application-id", "org.gbasic.one"),
          tag: "first" },
        { app: gi.new("Gio.Application", "application-id", "org.gbasic.two"),
          tag: "second" }
    ]
    for each e in rows
        e.app.application_id = "org.gbasic." + e.tag + ".written"
    next
    for each e in rows
        print e.tag + ": " + gi.get(e.app, "application-id")
    next
end program
