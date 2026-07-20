' NAP-0 precondition probe: the Native Application Platform is driven entirely
' through the `gi` bridge (GtkSourceView and GTK 4 are NOT linked), so its only
' extra runtime requirement over the base `gi` module is that the GTK 4 and
' GtkSource 5 introspection typelibs resolve. This program asserts exactly that.
' The runner (tests/run_native_platform.sh) treats a "could not load namespace"
' failure here as an environment SKIP, mirroring run_gi.sh's dependency gate.
load gi
gi.require("GLib", "2.0")
gi.require("Gio", "2.0")
gi.require("Gtk", "4.0")
gi.require("GtkSource", "5")
gi.require("Gdk", "4.0")
gi.require("Pango", "1.0")
print "native platform typelibs resolved"
