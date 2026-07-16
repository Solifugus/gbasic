load gi
gi.require("Gio", "2.0")
c = gi.new("Gio.Cancellable")
gi.get(c, "no-such-property")
