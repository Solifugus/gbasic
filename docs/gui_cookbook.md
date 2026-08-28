# GUI cookbook

Desktop UI in gBASIC, by task. Every recipe on this page is a file in
`examples/gui_cookbook/` that the test suite runs, and every output block is
that file's committed output — the page owns neither, so it cannot drift from
what the code actually does. See `docs/gui_tutorial.md` for the guided version.

**None of these show a window.** `gtk.init()` needs a display, but *showing*
does not, so each recipe builds real widgets and interrogates them. That is
also how the GUI test suites work, and it is why this cookbook can be checked
at all.

**The layers, and which to reach for.**

| | What it is | Reach for it when |
|---|---|---|
| `gtk` | ~12 constructors over the bridge — window, box, paned, notebook, listbox, button, label, scrolled | almost always; this is what gBASIC Studio is built from |
| `sourceeditor`, `datagrid` | two complex widgets as units | you need a code editor or a table |
| `gtkui` | a declarative reconciler for a keyed, changing region | a list that gains, loses and reorders rows |
| `gi` | the raw GObject-Introspection bridge | anything the above does not cover — which is most of GTK |

Studio uses `gtk` + `gi` imperatively and does not use `gtkui` at all. That is
not a criticism of the reconciler; it is what a mostly-fixed layout needs.

---

## 1. A first window

<!--CODE:01_first_window-->

```basic
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
```

<!--OUT:01_first_window-->

```
window holds a box : true
title text         : Notes
button text        : Add
box type name      : GtkBox
```

## 2. State in callbacks — read this one first

The single biggest trap. gBASIC has **no closures**, and GUI code is nothing
but callbacks.

<!--CODE:02_state_in_callbacks-->

```basic
' THE most important thing to know before writing a GUI in gBASIC.
'
' gBASIC HAS NO CLOSURES. A handler that assigns to a variable from an
' enclosing scope does not update it -- it silently creates a function-local of
' the same name, and the outer value never changes. GUI code is nothing but
' callbacks, so this is where it bites hardest: a counter that stays at 1, a
' flag that never flips, a loop that never stops.
'
' The fix is to keep state in a RECORD and mutate a field. A record is shared,
' so a handler can change what the rest of the program sees.
'
' The interpreter warns about the broken form (read-then-assign of an outer
' name) exactly once per site -- but only if you READ the variable first, which
' the broken version below does.
load gi
load gtk

gtk.require()
gtk.init()

' RIGHT: a shared record. The field survives every callback.
state = { ticks: 0, done: false }

function tick()
    state.ticks = state.ticks + 1
    print "  tick " + string(state.ticks)
    if state.ticks >= 3 then
        state.done = true
        gi.quit()
        return 0
    end if
    return 1                    ' non-zero keeps the timeout scheduled
end function

print "running a real event loop:"
gi.timeout(10, tick)
gi.main()

print "ticks after the loop : " + string(state.ticks)
print "done flag            : " + string(state.done)
print ""
print "A plain `ticks = ticks + 1` inside that handler would have left"
print "ticks at 0 and the loop would never have quit."
```

<!--OUT:02_state_in_callbacks-->

```
running a real event loop:
  tick 1
  tick 2
  tick 3
ticks after the loop : 3
done flag            : true

A plain `ticks = ticks + 1` inside that handler would have left
ticks at 0 and the loop would never have quit.
```

## 3. Connecting signals

<!--CODE:03_signals-->

```basic
' Wiring a widget to a handler. `gi.connect(widget, signal, handler)` takes a
' FUNCTION VALUE -- the bare name, not a call -- and returns a handler id you
' can pass to `gi.disconnect` later.
'
' Handlers cannot be triggered from gBASIC (the bridge exposes no signal-emit),
' so what a test can assert is the WIRING and anything the handler does when
' the loop drives it. Recipe 02 shows a handler actually running, via a
' timeout, which is the same mechanism a click uses.
load gi
load gtk

gtk.require()
gtk.init()

seen = { clicks: 0, last: "" }

function on_add(btn)
    seen.clicks = seen.clicks + 1
    seen.last = "add"
    return 0
end function

add = gtk.button("Add")
id = gi.connect(add, "clicked", on_add)

print "connected, handler id is a number : " + string(is_number(id))
print "id is non-zero                    : " + string(id != 0)

' `gtk.connect` is the same call under a friendlier name.
del = gtk.button("Delete")
id2 = gtk.connect(del, "clicked", on_add)
print "gtk.connect works the same        : " + string(is_number(id2))

' Disconnecting is by id, on the same widget.
gi.disconnect(add, id)
print "disconnected without error        : true"

' A handler is an ordinary function value: it can be stored and passed.
handlers = { add: on_add }
print "a handler stores in a record      : " + string(type(handlers.add))
```

<!--OUT:03_signals-->

```
connected, handler id is a number : true
id is non-zero                    : true
gtk.connect works the same        : true
disconnected without error        : true
a handler stores in a record      : function
```

## 4. Layout containers

<!--CODE:04_layout-->

```basic
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
```

<!--OUT:04_layout-->

```
outer is a box        : true
split is a paned      : true
left of the split     : GtkListBox
right of the split    : GtkNotebook
notebook pages        : 1
scrolled child        : GtkViewport
vertical enum value   : 1
```

## 5. A table of rows

<!--CODE:05_datagrid-->

```basic
' A table of rows. `datagrid` sits on a native row model, so a grid of a
' million rows realizes only the cells GTK actually asks for -- the point of
' the library is that virtualization, not the widget.
'
' A REGISTRY holds the callbacks and keeps them alive; without it a factory
' would be collected while GTK still holds a pointer to it. Assign it to
' `_DATAGRID` before creating anything.
load gi
load gtk
load datagrid

gtk.require()
gtk.init()

_DATAGRID = datagrid.new_registry()

rows = [
    { name: "ari.bas",   size: 41230 },
    { name: "chart.bas", size: 88104 },
    { name: "web.bas",   size: 52377 }
]

' EVERY datagrid update RETURNS THE NEW HANDLE and must be reassigned. gBASIC
' functions cannot change their caller, so an API that updates something hands
' back the updated value -- and the unused-result warning tells you when you
' forgot. `h = datagrid.add_column(h, ...)`, never a bare call.
h = datagrid.create(rows)
h = datagrid.add_column(h, { title: "File", field: "name" })
h = datagrid.add_column(h, { title: "Bytes", field: "size" })

print "rows            : " + string(datagrid.row_count(h))
print "cell 0,0        : " + datagrid.cell(h, 0, 0)
print "cell 1,1        : " + datagrid.cell(h, 1, 1)
print "widget type     : " + gi.type_name(datagrid.widget(h))

' The rows are a VALUE. Replacing them refreshes what the grid shows, and the
' original array is untouched -- gBASIC arrays are copy-on-write.
h = datagrid.set_rows(h, [{ name: "one.bas", size: 1 }])
print "after set_rows  : " + string(datagrid.row_count(h)) + " row, cell 0,0 = " + datagrid.cell(h, 0, 0)
print "original intact : " + string(count(rows)) + " rows"

h = datagrid.destroy(h)
print "destroyed       : " + string(datagrid.destroyed(h))
```

<!--OUT:05_datagrid-->

```
rows            : 3
cell 0,0        : ari.bas
cell 1,1        : 88104
widget type     : GtkColumnView
after set_rows  : 1 row, cell 0,0 = one.bas
original intact : 3 rows
destroyed       : true
```

## 6. A region that changes

<!--CODE:06_dynamic_region-->

```basic
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
```

<!--OUT:06_dynamic_region-->

```
mounted    : alpha, beta
updated    : ALPHA
reused     : true
inserted   : gamma
still same : true
b removed  : true
a survives : ALPHA
unmounted  : true
```

## 7. When `gtk` has no constructor for it

<!--CODE:07_escape_hatch-->

```basic
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
```

<!--OUT:07_escape_hatch-->

```
entry placeholder : search...
check label       : recursive
spinner type      : GtkSpinner
changed via method: filter…
changed via gi.set: case sensitive
Gio action name   : save
box holds them    : GtkEntry
```

## 8. Testing GUI code

<!--CODE:08_testing_gui_code-->

```basic
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
```

<!--OUT:08_testing_gui_code-->

```
buttons built : 3
in order      : New Open Save
all buttons   : true
container is  : GtkBox
empty toolbar : true
```

---

## Reporting quick reference

| You want | Reach for | Note |
|---|---|---|
| A window with widgets | `gtk.window`, `gtk.box`, `gtk.button`, `gtk.label` | `gtk.require()` then `gtk.init()` first |
| State across callbacks | a shared **record**, mutated by field | never an outer scalar — no closures |
| A handler on a widget | `gi.connect(w, "signal", fn)` | returns an id for `gi.disconnect` |
| A repeating action | `gi.timeout(ms, fn)` | return non-zero to stay scheduled |
| Split / tabs / scroll | `gtk.paned`, `gtk.notebook`, `gtk.scrolled` | `scrolled` inserts a `GtkViewport` |
| A table | `datagrid` | every update **returns the new handle** |
| A code editor | `sourceeditor` | needs the GtkSource typelib |
| A changing keyed list | `gtkui.mount` / `update` | keys are what make a move a move |
| Anything else in GTK | `gi.new("Gtk.Entry", "prop", value)` | flat name/value pairs, not a record |
| A missing key | `is_nothing(...)` | `nothing`, not `unknown` |

---

*Recipes are executed by `tests/run_gui_cookbook.sh`, which skips cleanly on a
machine with no display or no GTK 4 typelib. What these prove is structure and
wiring; they cannot prove that a layout is legible or reachable by keyboard,
and the bridge exposes no way to emit a signal, so click handlers are driven by
a timeout or verified by hand.*
