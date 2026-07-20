' NAP-2: a callable with a single scalar OUT parameter plus a real (bool) return
' value. GLib.ascii_string_to_unsigned(str, base, min, max, OUT out_num) returns a
' gboolean and writes the parsed value through out_num (a guint64 out-param).
'
' Calling convention: because there is BOTH a real return AND an out-param, the
' result is a record. The primary return is under the key `result`; each out-param
' is under its own introspected name (here `out_num`). (`result` is used rather than
' the reserved word `return`.)
load gi
gi.require("GLib", "2.0")

r = gi.invoke("GLib.ascii_string_to_unsigned", "42", 10, 0, 100)
print r.result
print r.out_num
