' NAP-3 negative: gi.source_remove on an id that names no live source is a clean
' error (the id is validated before g_source_remove, which would otherwise emit a
' fatal GLib critical under fatal-criticals).
load gi
gi.require("GLib", "2.0")
gi.source_remove(999999)
