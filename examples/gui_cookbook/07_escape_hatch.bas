' What to do when `gtk.bas` has no constructor for what you need.
'
' gtk.bas wraps a dozen widgets -- the ones an application reaches for
' constantly. Everything else in GTK, and everything in every other
' GObject library on the machine, comes through `gi` directly. That is the
' point of the bridge: the curated layer never has to be complete.
load gi
load gtk

gtk.require()
gtk.init()

' `gi.new` takes a qualified type name then construct-time properties as FLAT
' NAME/VALUE PAIRS -- not a record. Property names use the GObject spelling
' with hyphens, though underscores are accepted.
entry = gi.new("Gtk.Entry", "placeholder-text", "search...")
check = gi.new("Gtk.CheckButton", "label", "recursive")
spin = gi.new("Gtk.Spinner")

print "entry placeholder : " + entry.get_placeholder_text()
print "check label       : " + check.get_label()
print "spinner type      : " + gi.type_name(spin)

' Properties after construction: the widget's own methods, or gi.get/gi.set
' for anything without one.
entry.set_placeholder_text("filter…")
print "changed via method: " + entry.get_placeholder_text()
gi.set(check, "label", "case sensitive")
print "changed via gi.set: " + gi.get(check, "label")

' A namespace other than Gtk. The bridge is not GTK-specific -- any GObject
' library with a typelib is reachable, which is how an application gets
' actions, settings or streams. (`gi.new` constructs; `gi.invoke` calls a
' NAMESPACE-level function, and not every static constructor is exposed as
' one -- when `gi.invoke` reports "unknown function", reach for `gi.new`.)
act = gi.new("Gio.SimpleAction", "name", "save")
print "Gio action name   : " + gi.get(act, "name")

' Mixing is normal: a gtk.bas container holding gi.new children.
box = gtk.box("v", 4)
box.append(entry)
box.append(check)
print "box holds them    : " + gi.type_name(box.get_first_child())
