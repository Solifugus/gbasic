' Lifetime accounting: an object created INSIDE a signal handler must survive the
' handler's scope when an EXTERNAL owner holds a strong reference — the handler's
' local wrapper being freed on return must not destroy it. This models (headlessly,
' no display) the GtkApplicationWindow-in-activate case: there the window's floating
' ref is adopted by the GtkApplication during construction, so gi.new must take its
' OWN ref rather than co-owning the app's, or dropping the handler-local wrapper
' destroys the app's window. Here the app strong-refs a Gio.SimpleAction via
' add_action; after activate returns (its `act` wrapper gone) the action is still
' alive and fully usable.
load gi
gi.require("Gio", "2.0")

function on_activate(a)
    act = gi.new("Gio.SimpleAction", "name", "greet")
    gi.call(a, "add_action", act)
end function

app = gi.new("Gio.Application", "application-id", "org.gbasic.RegTest")
gi.call(app, "register", nothing)
gi.connect(app, "activate", on_activate)
gi.call(app, "activate")

found = gi.call(app, "lookup_action", "greet")
print(gi.type_name(found))
print(gi.is_a(found, "Gio.SimpleAction"))
print(gi.get(found, "name"))
print(gi.get(found, "enabled"))
