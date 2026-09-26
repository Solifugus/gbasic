' THE OTHER HALF, and the one that needs a display: once the toolkit IS
' initialized a widget constructs exactly as before. Without this the refusal
' would be indistinguishable from "gi.new can no longer make a widget".
'
' The Gtk.Application route is the one that matters most and is asserted here
' too: GTK initializes ITSELF inside `activate`, where this bridge never sees it
' happen, so the check asks `Gtk.is_initialized` rather than tracking a flag of
' its own -- a flag set by our `gtk.init()` would refuse the construction every
' Studio run performs.
load gi
load gtk

function on_activate(app)
    b = gi.new("Gtk.Button")
    print "activate: " + gi.type_name(b)
    gi.call(app, "quit")
    return nothing
end function

program main(args)
    gtk.init()
    gi.require("Gtk", "4.0")
    print "explicit: " + gi.type_name(gi.new("Gtk.DropDown"))

    app = gi.new("Gtk.Application", "application-id", "org.gbasic.WidgetInit")
    gi.connect(app, "activate", on_activate)
    ignored = gi.call(app, "run", 0, nothing)
end program
