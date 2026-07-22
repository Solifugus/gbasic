' Typelib probe for the gtkui smoke tier: the reconciler is driven entirely
' through the `gi` bridge (GTK 4 is NOT linked), so its only extra requirement
' over the base `gi` module is that the GTK 4 introspection typelib resolves.
' The runner treats a "could not load namespace" failure as an environment SKIP.
load gi
gi.require("Gtk", "4.0")
print "gtk4 typelib resolved"
