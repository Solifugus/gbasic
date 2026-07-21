' NAP-5: GObject method calls via `.method()` sugar. Routes through the same
' gi_invoke_callable machinery as gi.call (NAP-2 out/inout/GError semantics),
' including methods inherited from ancestor classes.
load gi
gi.require("Gio", "2.0")

c = gi.new("Gio.Cancellable")
print c.is_cancelled()
c.cancel()
print c.is_cancelled()

' close is inherited from Gio.InputStream (ancestor walk must find it)
s = gi.new("Gio.MemoryInputStream")
print s.close(nothing)
