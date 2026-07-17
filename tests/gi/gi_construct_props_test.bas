' Construct-time properties (Item 1). Gio.SimpleAction's "name" is construct-only
' (flags include CONSTRUCT_ONLY), so it can ONLY be set through gi.new — this is
' the exact fixture the Phase-GI tests-first could not build with the bare gi.new,
' forcing the Cancellable detour. Setting it at construction closes that loop.
load gi
gi.require("Gio", "2.0")

a = gi.new("Gio.SimpleAction", "name", "save")
print(gi.get(a, "name"))
print(gi.is_a(a, "Gio.SimpleAction"))
