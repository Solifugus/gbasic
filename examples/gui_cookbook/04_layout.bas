' Containers. `gtk.bas` wraps the handful a real application actually reaches
' for; anything else comes from `gi.new` (recipe 09).
'
' Orientation is a STRING here -- "v" / "h" -- rather than the GTK enum, which
' is the one place gtk.bas deliberately differs from the C API. `gtk.enum`
' still gives you the raw value when a call needs it.
load gi
load gtk

gtk.require()
gtk.init()

' A vertical box holding a header and a body.
outer = gtk.box("v", 0)
header = gtk.box("h", 6)
header.append(gtk.label("gBASIC Studio"))
header.append(gtk.button("Run"))
outer.append(header)

' A paned split: a navigator on the left, documents on the right.
split = gtk.paned("h")
nav = gtk.listbox()
book = gtk.notebook()
split.set_start_child(nav)
split.set_end_child(book)
outer.append(split)

' A scrolled window wraps a child that may be larger than its slot.
long_text = gtk.label("...a very long document...")
scroller = gtk.scrolled(long_text)
book.append_page(scroller, gtk.label("doc.bas"))

print "outer is a box        : " + string(gi.is_a(outer, "Gtk.Box"))
print "split is a paned      : " + string(gi.is_a(split, "Gtk.Paned"))
print "left of the split     : " + gi.type_name(split.get_start_child())
print "right of the split    : " + gi.type_name(split.get_end_child())
print "notebook pages        : " + string(book.get_n_pages())
print "scrolled child        : " + gi.type_name(scroller.get_child())
print "vertical enum value   : " + string(gtk.enum("Gtk.Orientation.VERTICAL"))
