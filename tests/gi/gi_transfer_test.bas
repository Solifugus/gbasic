' Reference-lifetime / transfer handling. c2 = c shares one underlying GObject
' through a single wrapper (value_copy bumps the wrapper refcount, not the
' object); mutating through one handle is visible through the other, and neither
' handle frees the object prematurely.
load gi
gi.require("Gio", "2.0")

c = gi.new("Gio.Cancellable")
c2 = c
print(gi.call(c, "is_cancelled"))
gi.call(c, "cancel")
print(gi.call(c2, "is_cancelled"))
