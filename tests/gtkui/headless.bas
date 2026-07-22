' NAP-11 gtkui reconciler — headless pure-logic test. Exercises the diff/
' classification helpers that need no widgets, GTK init, or display: the
' container-kind table, the all-keyed matcher predicate, the reuse-vs-replace
' compatibility test (type-based branch), and the record-field default helper.
load gi
load gtkui

' container-kind classification
print gtkui._kind("Gtk.Box")
print gtkui._kind("Gtk.ScrolledWindow")
print gtkui._kind("Gtk.Window")
print gtkui._kind("Gtk.Paned")
print gtkui._kind("Gtk.Button")

' all-keyed predicate: keyed matching only when every child carries a key
olds = [ {native:false, type:"Gtk.Label", key:"a"}, {native:false, type:"Gtk.Label", key:"b"} ]
news = [ {type:"Gtk.Label", key:"a"}, {type:"Gtk.Label", key:"b"} ]
print gtkui._all_keyed(olds, news)
news2 = [ {type:"Gtk.Label", key:"a"}, {type:"Gtk.Label"} ]
print gtkui._all_keyed(olds, news2)

' compatibility (type-based branch): same type reuses, different type replaces,
' native-vs-constructed mismatch replaces
print gtkui._compatible({native:false, type:"Gtk.Box", key:"x"}, {type:"Gtk.Box"})
print gtkui._compatible({native:false, type:"Gtk.Box", key:"x"}, {type:"Gtk.Label"})
print gtkui._compatible({native:false, type:"Gtk.Box"}, {widget:nothing})

' record-field default helper
print gtkui._get({a:5}, "a", 99)
print gtkui._get({a:5}, "b", 99)
