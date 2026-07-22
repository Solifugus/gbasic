' NAP-7 display smoke: the GtkSourceView-dependent surface. Requires an
' initialized GTK and a display, so the runner executes it only when a display is
' present (and skips cleanly otherwise). Proves the view is reachable, scrolling
' works, and a GtkTextChildAnchor inline widget can be anchored at a source line
' while the view stays editable.
load gi
load gtk
load sourceeditor
gtk.init()

ed = sourceeditor.create()
ed.set_text("print(\"hi\")\nx = 1\ny = 2\nz = 3\nw = 4\n")
ed.set_language("gbasic")

v = ed.view()
print gi.type_name(v)

ed.scroll_to(4)
print "scrolled"

btn = gtk.button("run")
anchor = ed.add_inline(1, btn)
print gi.type_name(anchor)
print v.get_editable()
print "done"
