' NAP-2: the GtkTextIter bridge proof — a caller-allocates struct OUT parameter.
' This is the capability that makes GtkTextBuffer/GtkSourceView practical.
'
' GtkTextBuffer is a plain GObject (no display needed). get_start_iter / get_end_iter
' each have a single caller-allocates GtkTextIter OUT param and a void return, so by
' the calling convention each returns that one out value directly (no wrapping record).
' The returned iters are boxed values (VALUE_GBOXED), which we then pass straight back
' into get_text as ordinary boxed IN args — a full out-struct round-trip.
load gi
gi.require("Gtk", "4.0")

buf = gi.new("Gtk.TextBuffer")
gi.call(buf, "set_text", "hello world", -1)

s = gi.call(buf, "get_start_iter")
e = gi.call(buf, "get_end_iter")
print type(s)
print type(e)

txt = gi.call(buf, "get_text", s, e, false)
print txt
