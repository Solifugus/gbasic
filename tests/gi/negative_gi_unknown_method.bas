load gi
gi.require("Gio", "2.0")
c = gi.new("Gio.Cancellable")
gi.call(c, "no_such_method")
