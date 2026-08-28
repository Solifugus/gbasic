' A window, a box, and two widgets -- built, wired together, and inspected.
'
' Nothing is SHOWN. Every recipe here builds a widget tree and asks it
' questions, which is how GUI code is tested: `gtk.init()` needs a display but
' showing a window does not, so a suite can assert on real widgets without a
' human. See recipe 10.
load gi
load gtk

gtk.require()
gtk.init()

win = gtk.window()
box = gtk.box("v", 6)
title = gtk.label("Notes")
add = gtk.button("Add")

box.append(title)
box.append(add)
win.set_child(box)

print "window holds a box : " + string(gi.is_a(win.get_child(), "Gtk.Box"))
print "title text         : " + title.get_label()
print "button text        : " + add.get_label()
print "box type name      : " + gi.type_name(box)
