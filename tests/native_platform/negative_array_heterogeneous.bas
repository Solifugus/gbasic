' NAP-4 negative: an array passed into a GStrv arg must be homogeneously string-
' compatible; a number in the middle is rejected (no silent coercion), and any
' partially-built native container is cleaned up.
load gi
gi.require("GLib", "2.0")
joined = gi.invoke("GLib.strjoinv", "-", ["x", 5, "z"])
