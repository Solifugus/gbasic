' Signal dispatch: connect a gBASIC function to a GObject signal by name and
' emit it via a method call. Uses Gio.Cancellable (headless, always present):
' the "cancelled" signal fires synchronously from g_cancellable_cancel().
load gi
gi.require("Gio", "2.0")

c = gi.new("Gio.Cancellable")

function on_cancelled(source)
    print("cancelled fired")
    ' The emitter is passed as the first signal argument. qdata canonicalization
    ' means it maps back to the very same wrapper we connected on.
    if source = c then
        print("same object")
    else
        print("different object")
    end if
end function

gi.connect(c, "cancelled", on_cancelled)
gi.call(c, "cancel")
print("done")
