' Typelib probe for the DataGrid display tier. The high-level grid is driven
' through the `gi` bridge over the native rowmodel adapter (GTK 4 is NOT linked),
' so its only extra requirement over the base modules is that the GTK 4
' introspection typelib resolves. The runner treats a "could not load namespace"
' failure as an environment SKIP.
load gi
gi.require("Gtk", "4.0")
print "gtk4 typelib resolved"
