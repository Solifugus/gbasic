' NAP-11 gtkui reconciler — display smoke test. Exercises mount, property
' update with instance reuse, keyed insert/remove/reorder, type replacement,
' a 3-level nested tree, the native-widget escape hatch, single-fire signals
' after repeated reconciliation, and unmount. Runs under a display with
' G_DEBUG=fatal-criticals; widgets are built but never shown (no UI needed).
load gi
load gtkui

gi.require("Gtk", "4.0")
gi.invoke("Gtk.init")

VBOX = gi.enum("Gtk.Orientation.VERTICAL")
HBOX = gi.enum("Gtk.Orientation.HORIZONTAL")

function bump()
    print "bump"
end function

' ---- 1. initial mount -----------------------------------------------------
win = gi.new("Gtk.Window")
desc1 = { type: "Gtk.Box", props: { orientation: VBOX, spacing: 6 }, children: [
    { type: "Gtk.Label", key: "greet", props: { label: "hello" } },
    { type: "Gtk.Button", key: "go", props: { label: "Go" } }
] }
h = gtkui.mount(win, desc1)
root = gtkui.root(h)
print "root type: " + gi.type_name(root)
greet = gtkui.lookup(h, "greet")
gobtn = gtkui.lookup(h, "go")
print "greet: " + greet.get_label()
print "go: " + gobtn.get_label()
wchild = win.get_child()
print "win child is the box: " + string(gi.is_a(wchild, "Gtk.Box"))

' stamp a CSS name to prove instance reuse across later updates
greet0 = gtkui.lookup(h, "greet")
greet0.set_name("greet-instance")

' ---- 2. property update (same instance reused) ----------------------------
desc2 = { type: "Gtk.Box", props: { orientation: VBOX, spacing: 6 }, children: [
    { type: "Gtk.Label", key: "greet", props: { label: "hi there" } },
    { type: "Gtk.Button", key: "go", props: { label: "Go" } }
] }
h = gtkui.update(h, desc2)
g2 = gtkui.lookup(h, "greet")
print "greet text updated: " + g2.get_label()
print "greet reused (name persists): " + g2.get_name()
print "greet identity equal: " + string(g2 = greet0)

' ---- 3. keyed child insert ------------------------------------------------
desc3 = { type: "Gtk.Box", props: { orientation: VBOX, spacing: 6 }, children: [
    { type: "Gtk.Label", key: "greet", props: { label: "hi there" } },
    { type: "Gtk.Label", key: "mid", props: { label: "middle" } },
    { type: "Gtk.Button", key: "go", props: { label: "Go" } }
] }
h = gtkui.update(h, desc3)
midw = gtkui.lookup(h, "mid")
print "mid inserted: " + midw.get_label()
g3 = gtkui.lookup(h, "greet")
print "greet still reused: " + g3.get_name()

' name the three so we can read child order by CSS name
nw = gtkui.lookup(h, "greet")
nw.set_name("n-greet")
nw = gtkui.lookup(h, "mid")
nw.set_name("n-mid")
nw = gtkui.lookup(h, "go")
nw.set_name("n-go")

' ---- 4. keyed reorder (identity preserved) --------------------------------
desc4 = { type: "Gtk.Box", props: { orientation: VBOX, spacing: 6 }, children: [
    { type: "Gtk.Button", key: "go", props: { label: "Go" } },
    { type: "Gtk.Label", key: "greet", props: { label: "hi there" } },
    { type: "Gtk.Label", key: "mid", props: { label: "middle" } }
] }
h = gtkui.update(h, desc4)
rootb = gtkui.root(h)
c = rootb.get_first_child()
order = c.get_name()
c = c.get_next_sibling()
order = order + "," + c.get_name()
c = c.get_next_sibling()
order = order + "," + c.get_name()
print "order after reorder: " + order
g4 = gtkui.lookup(h, "greet")
print "greet identity after reorder: " + string(g4 = greet0)

' ---- 5. keyed child remove ------------------------------------------------
desc5 = { type: "Gtk.Box", props: { orientation: VBOX, spacing: 6 }, children: [
    { type: "Gtk.Button", key: "go", props: { label: "Go" } },
    { type: "Gtk.Label", key: "greet", props: { label: "hi there" } }
] }
h = gtkui.update(h, desc5)
print "mid removed: " + string(gtkui.lookup(h, "mid") = nothing)

' ---- 6. type replacement under the same key -------------------------------
desc6 = { type: "Gtk.Box", props: { orientation: VBOX, spacing: 6 }, children: [
    { type: "Gtk.Label", key: "go", props: { label: "now-a-label" } },
    { type: "Gtk.Label", key: "greet", props: { label: "hi there" } }
] }
h = gtkui.update(h, desc6)
gorep = gtkui.lookup(h, "go")
print "go replaced type: " + gi.type_name(gorep)
print "go new label: " + gorep.get_label()

' ---- 7. nested 3-level tree -----------------------------------------------
win2 = gi.new("Gtk.Window")
descN = { type: "Gtk.Box", props: { orientation: VBOX }, children: [
    { type: "Gtk.ScrolledWindow", key: "sw", children: [
        { type: "Gtk.Box", key: "inner", props: { orientation: HBOX }, children: [
            { type: "Gtk.Label", key: "deep", props: { label: "deep-value" } }
        ] }
    ] }
] }
hn = gtkui.mount(win2, descN)
deepw = gtkui.lookup(hn, "deep")
print "deep label: " + deepw.get_label()
sww = gtkui.lookup(hn, "sw")
print "sw is scrolled: " + string(gi.is_a(sww, "Gtk.ScrolledWindow"))
innerw = gtkui.lookup(hn, "inner")
print "inner is box: " + string(gi.is_a(innerw, "Gtk.Box"))

' ---- 8. native-widget escape hatch ----------------------------------------
native_entry = gi.new("Gtk.Entry")
native_entry.set_name("native-entry")
win3 = gi.new("Gtk.Window")
descE = { type: "Gtk.Box", props: { orientation: VBOX }, children: [
    { type: "Gtk.Label", key: "lab", props: { label: "form" } },
    { widget: native_entry, key: "entry" }
] }
he = gtkui.mount(win3, descE)
embedded = gtkui.lookup(he, "entry")
print "native embedded name: " + embedded.get_name()
print "native is same object: " + string(embedded = native_entry)
print "native is an entry: " + string(gi.is_a(embedded, "Gtk.Entry"))

' ---- 9. single-fire signal after repeated reconciliation ------------------
win4 = gi.new("Gtk.Window")
descS = { type: "Gtk.Box", props: { orientation: VBOX }, children: [
    { type: "Gtk.Button", key: "b", props: { label: "x" }, signals: { activate: bump } }
] }
hs = gtkui.mount(win4, descS)
hs = gtkui.update(hs, descS)
hs = gtkui.update(hs, descS)
print "activating once (expect a single bump):"
sbtn = gtkui.lookup(hs, "b")
sbtn.activate()

' ---- 10. unmount ----------------------------------------------------------
gtkui.unmount(hs)
print "unmounted"
