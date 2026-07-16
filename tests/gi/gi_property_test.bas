' Property get/set roundtrip + type introspection, on a headless GObject
' (Gio.Application constructs bare and has a read/write "application-id").
load gi
gi.require("Gio", "2.0")

app = gi.new("Gio.Application")
gi.set(app, "application-id", "org.gbasic.Test")
print(gi.get(app, "application-id"))
print(gi.type_name(app))
print(gi.is_a(app, "Gio.Application"))
print(gi.is_a(app, "Gio.Cancellable"))
