' NAP-4 (WI-7): a gBASIC string array passed INTO a GI function taking a
' zero-terminated GStrv (transfer none). GLib.strjoinv("-", ["x","y","z"]) -> "x-y-z".
' The native NULL-terminated char** is built with borrowed element pointers and freed
' (container only) after the call.
load gi
gi.require("GLib", "2.0")

joined = gi.invoke("GLib.strjoinv", "-", ["x", "y", "z"])
print joined
