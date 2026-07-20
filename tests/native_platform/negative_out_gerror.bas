' NAP-2 negative: a callable that reports failure through GError**. When
' GLib.ascii_string_to_unsigned is given a value outside [min,max] it returns FALSE
' and sets a GError. That must surface as a raised gBASIC runtime error carrying the
' GError message (and NO leaked GError, verified separately under valgrind). No out
' record is produced for a failed call.
load gi
gi.require("GLib", "2.0")
x = gi.invoke("GLib.ascii_string_to_unsigned", "999", 10, 0, 100)
