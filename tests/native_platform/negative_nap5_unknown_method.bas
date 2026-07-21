' NAP-5 negative: calling an unknown method through the sugar is a clean error.
load gi
gi.require("Gio", "2.0")
c = gi.new("Gio.Cancellable")
c.no_such_method()
