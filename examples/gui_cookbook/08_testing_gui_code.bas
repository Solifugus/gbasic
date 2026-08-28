' How to test a GUI without a human, and what that can and cannot prove.
'
' `gtk.init()` needs a DISPLAY, but SHOWING a window does not -- so a suite can
' construct real widgets, wire them, and interrogate them, all without anything
' appearing on screen. Every recipe in this cookbook is that test. It is also
' how tests/run_gtkui.sh and tests/run_datagrid.sh work.
'
' WHAT THIS PROVES: structure, properties, containment, that a handler is
' connected, that a reconciler reused rather than rebuilt a widget, that a grid
' reports the cells it should.
'
' WHAT IT DOES NOT PROVE: that anything is legible, correctly sized, or
' reachable by keyboard. The bridge exposes no way to emit a signal, so a click
' handler cannot be fired from gBASIC either -- drive behaviour through a
' timeout (recipe 02) or verify it by hand.
'
' Run GUI suites under G_DEBUG=fatal-criticals so a GTK criticial -- the
' warnings that mean "you have used this API wrongly" -- aborts instead of
' scrolling past.
load gi
load gtk

gtk.require()
gtk.init()

' Build the thing under test exactly as the application would.
function build_toolbar(labels)
    bar = gtk.box("h", 6)
    for each t in labels
        bar.append(gtk.button(t))
    next t
    return bar
end function

bar = build_toolbar(["New", "Open", "Save"])

' Then ask it questions. Walking children is how you assert structure.
n = 0
child = bar.get_first_child()
names = ""
while not is_nothing(child)
    n = n + 1
    names = names + child.get_label() + " "
    child = child.get_next_sibling()
end while

print "buttons built : " + string(n)
print "in order      : " + trim(names)
print "all buttons   : " + string(gi.is_a(bar.get_first_child(), "Gtk.Button"))
print "container is  : " + gi.type_name(bar)

' An empty case is worth a test of its own: a toolbar with no buttons must be
' an empty box, not a crash.
empty = build_toolbar([])
print "empty toolbar : " + string(is_nothing(empty.get_first_child()))
