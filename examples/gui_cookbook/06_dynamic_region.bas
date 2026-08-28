' A region that CHANGES as state changes -- a list that gains, loses and
' reorders rows. This is what `gtkui` is for: you describe the tree you want as
' a record, and it works out the minimum set of widget operations to get there.
'
' Where it earns its keep is KEYS. Give each child a `key` and the reconciler
' matches by that, so a row that moved is MOVED rather than destroyed and
' rebuilt -- which is the difference between a widget keeping its scroll
' position, selection and focus, and not.
'
' Studio does NOT use this: its shell is built imperatively with gtk.bas
' constructors, because a fixed layout has nothing to reconcile. Reach for
' gtkui for the list-shaped parts, not for the frame around them.
load gi
load gtkui

gi.require("Gtk", "4.0")
gi.invoke("Gtk.init")

win = gi.new("Gtk.Window")

' Mount an initial tree.
first = { type: "Gtk.Box", props: { orientation: 1, spacing: 4 }, children: [
    { type: "Gtk.Label", key: "a", props: { label: "alpha" } },
    { type: "Gtk.Label", key: "b", props: { label: "beta" } }
] }
h = gtkui.mount(win, first)
alpha = gtkui.lookup(h, "a")
alpha.set_name("original-instance")
print "mounted    : " + gtkui.lookup(h, "a").get_label() + ", " + gtkui.lookup(h, "b").get_label()

' Update a property. The SAME widget is reused -- the CSS name we stamped on it
' is still there, which is how you can tell it was not rebuilt.
second = { type: "Gtk.Box", props: { orientation: 1, spacing: 4 }, children: [
    { type: "Gtk.Label", key: "a", props: { label: "ALPHA" } },
    { type: "Gtk.Label", key: "b", props: { label: "beta" } }
] }
h = gtkui.update(h, second)
print "updated    : " + gtkui.lookup(h, "a").get_label()
print "reused     : " + string(gtkui.lookup(h, "a").get_name() = "original-instance")

' Insert in the middle and reorder. Keys make both cheap.
third = { type: "Gtk.Box", props: { orientation: 1, spacing: 4 }, children: [
    { type: "Gtk.Label", key: "b", props: { label: "beta" } },
    { type: "Gtk.Label", key: "c", props: { label: "gamma" } },
    { type: "Gtk.Label", key: "a", props: { label: "ALPHA" } }
] }
h = gtkui.update(h, third)
print "inserted   : " + gtkui.lookup(h, "c").get_label()
print "still same : " + string(gtkui.lookup(h, "a").get_name() = "original-instance")

' Remove one.
fourth = { type: "Gtk.Box", props: { orientation: 1, spacing: 4 }, children: [
    { type: "Gtk.Label", key: "a", props: { label: "ALPHA" } }
] }
h = gtkui.update(h, fourth)
' A key that is gone looks up as `nothing` -- not `unknown`. `nothing` is
' "no such thing"; `unknown` is "a value nobody knows". A removed widget is
' the first.
print "b removed  : " + string(is_nothing(gtkui.lookup(h, "b")))
print "a survives : " + gtkui.lookup(h, "a").get_label()

h = gtkui.unmount(h)
print "unmounted  : true"
