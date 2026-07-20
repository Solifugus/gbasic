' NAP-3 negative: gi.timeout requires a function handler; a non-function is a clean
' runtime error, not a crash.
load gi
gi.require("GLib", "2.0")
gi.timeout(5, 42)
