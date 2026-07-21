' NAP-5 negative: assigning an incompatible value to a property is a clean error,
' not a silent coercion (a gobject cannot become the string "application-id").
load gi
gi.require("Gio", "2.0")
app = gi.new("Gio.Application")
other = gi.new("Gio.Cancellable")
app.application_id = other
