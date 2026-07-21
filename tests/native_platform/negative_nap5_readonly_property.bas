' NAP-5 negative: writing a read-only property is a clean error (the setter
' pre-checks G_PARAM_WRITABLE so a GLib critical never fires).
load gi
gi.require("Gio", "2.0")
app = gi.new("Gio.Application")
app.is_registered = true
