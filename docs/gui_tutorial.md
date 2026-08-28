# Desktop applications in gBASIC

A guided introduction to writing a GTK 4 desktop application. Task-by-task
recipes, each executed by the test suite, are in
[`docs/gui_cookbook.md`](gui_cookbook.md); this page is the narrative.

**What you need:** GTK 4 and its typelib, plus a display.

```sh
sudo apt-get install gir1.2-gtk-4.0 libgtk-4-1        # Debian/Ubuntu
```

The interpreter must have been built with GObject-Introspection available;
`make` detects it. A build with it compiled out reports a clean runtime error
on `load gi` rather than failing to build.

---

## 1. The layers, and which one you are actually using

There are four, and they stack. It matters that you know which one you are in,
because the error messages come from different places.

| | What it is |
|---|---|
| **`gi`** | The GObject-Introspection bridge. Any GObject library on the machine — GTK, Gio, GtkSource — reachable by name, with no binding code written for it. |
| **`gtk`** | About a dozen constructors over `gi` for the widgets an application reaches for constantly. |
| **`sourceeditor`, `datagrid`** | Two complex widgets packaged as units: a GtkSourceView editor, and a virtualizing table. |
| **`gtkui`** | A declarative reconciler: describe the widget tree you want, and it computes the minimum set of operations to get there. |

**Start with `gtk` and `gi`.** That is not a hedge — it is what gBASIC Studio,
the largest application written in gBASIC, actually does: 86 `gtk.` calls, 72
`gi.` calls, and no `gtkui` at all. A fixed layout has nothing to reconcile.
Reach for `gtkui` when you have a *changing, keyed list*; reach for `gi.new`
whenever `gtk` has no constructor for what you want, which will be often, and
which is fine.

---

## 2. A first window

```basic
load gi
load gtk

gtk.require()
gtk.init()

win = gtk.window()
box = gtk.box("v", 6)
box.append(gtk.label("Notes"))
box.append(gtk.button("Add"))
win.set_child(box)
```

`gtk.require()` loads the GTK namespace; `gtk.init()` initialises the library
and needs a display. Both are safe to call more than once.

Widgets are ordinary gBASIC values holding a GObject. Their **methods are the
C API**, called with a dot: `win.set_child(box)`, `btn.get_label()`,
`box.append(child)`. If you know GTK, you already know this; if you do not, the
GTK documentation applies directly, which is the whole reason the bridge exists.

`gtk.box` takes `"v"` or `"h"` rather than the GTK orientation enum — the one
place `gtk` deliberately differs from the C API. When a call needs the raw
value, `gtk.enum("Gtk.Orientation.VERTICAL")` gives it.

---

## 3. The thing that will bite you: no closures

Read this before writing a single handler.

**gBASIC has no closures.** A function that assigns to a variable from an
enclosing scope does not update it — it silently creates a function-local of
the same name. Desktop code is nothing but callbacks, so this is where the rule
does the most damage: a counter that stays at 1, a flag that never flips, a
loop that never ends.

```basic
' WRONG -- `ticks` never changes outside this function
ticks = 0
function tick()
    ticks = ticks + 1
end function
```

```basic
' RIGHT -- a record is shared, so a field mutation is visible everywhere
state = { ticks: 0 }
function tick()
    state.ticks = state.ticks + 1
end function
```

The interpreter warns when you read an outer variable and then assign the same
name — once per site, so a loop cannot bury you in advice. It cannot warn about
a blind assignment, because that is indistinguishable from an ordinary new
local. **Keep application state in one record and pass it nowhere**: handlers
see it because it is in scope, and mutating a field is how they change it.

---

## 4. Signals

```basic
function on_add(btn)
    state.count = state.count + 1
    return 0
end function

add = gtk.button("Add")
id = gi.connect(add, "clicked", on_add)
```

`gi.connect` takes a **function value** — the bare name, no parentheses — and
returns a handler id you can later pass to `gi.disconnect(widget, id)`.
`gtk.connect` is the same call.

Handlers take the emitting widget and return a number. There is no way to emit
a signal from gBASIC, so you cannot fire a click from code: drive behaviour
through a timeout (below), or verify it by hand.

---

## 5. The event loop

Two shapes, and you pick one.

**The plain loop** — `gi.main()` runs until something calls `gi.quit()`:

```basic
gi.timeout(60, poll)          ' every 60ms; return non-zero to stay scheduled
gi.main()
```

`gi.timeout` is how a desktop application does anything periodic without
blocking — polling a child process, animating, autosaving. Studio uses exactly
this to watch a running program. Returning `0` from the callback cancels it.

**The application** — `Gtk.Application` drives its own loop, emits `activate`,
and quits when the last window closes:

```basic
app = gtk.application("org.example.notes")
gi.connect(app, "activate", on_activate)
code = gi.call(app, "run", 0, nothing)
```

Build and show your window inside the `activate` handler; that is the contract.
Use this shape for anything you would install — it gets you a desktop identity,
single-instance behaviour and session integration for free.

---

## 6. Layout

`gtk` wraps the containers that carry an application's frame:

- `gtk.box(orientation, spacing)` — the workhorse
- `gtk.paned(orientation)` — a draggable split, with `set_start_child` / `set_end_child`
- `gtk.notebook()` — tabs, via `append_page(child, label_widget)`
- `gtk.listbox()` — a simple vertical list of rows
- `gtk.scrolled(child)` — scrolling. Note it inserts a `GtkViewport`, so
  `get_child()` returns the viewport, not what you put in.
- `gtk.stack()` — one visible child at a time

Anything else is `gi.new`.

---

## 7. The two complex widgets

**`datagrid`** is a table. Its value is virtualization: a million rows realize
only the cells GTK asks for. Create a registry first — it keeps the callbacks
alive that GTK holds pointers to — and remember that **every update returns the
new handle**:

```basic
_DATAGRID = datagrid.new_registry()
h = datagrid.create(rows)
h = datagrid.add_column(h, { title: "File", field: "name" })
h = datagrid.set_rows(h, newer_rows)
```

A bare `datagrid.add_column(h, …)` warns, and rightly: gBASIC functions cannot
change their caller, so an update hands back the updated value.

**`sourceeditor`** is a GtkSourceView editor with gBASIC syntax highlighting
(`stdlib/gtksourceview/gbasic.lang`). It needs the GtkSource typelib in
addition to GTK.

---

## 8. Regions that change

When part of the UI is a list that gains, loses and reorders items, describing
it beats mutating it. `gtkui` takes a record tree and reconciles:

```basic
desc = { type: "Gtk.Box", props: { orientation: 1, spacing: 4 }, children: [
    { type: "Gtk.Label", key: "a", props: { label: "alpha" } },
    { type: "Gtk.Label", key: "b", props: { label: "beta" } }
] }
h = gtkui.mount(parent, desc)
h = gtkui.update(h, next_desc)
```

**Keys are the point.** With a `key`, a child that moved is *moved* rather than
destroyed and rebuilt — which is the difference between a widget keeping its
selection, scroll position and focus, and losing them. `gtkui.lookup(h, key)`
finds a live widget; a key that is gone comes back `nothing`.

Use this for the list-shaped parts, not the frame around them.

---

## 9. Testing

`gtk.init()` needs a display, but *showing* a window does not. So a test can
build real widgets, wire them and interrogate them with nothing on screen —
which is how every recipe in the cookbook, and the `gtkui` and `datagrid`
suites, are checked.

Run GUI tests under `G_DEBUG=fatal-criticals` so a GTK critical — the class of
warning that means "you have used this API wrongly" — aborts rather than
scrolling past into a golden.

What this proves is structure and wiring. It does not prove a layout is
legible, correctly sized, or reachable by keyboard; those still need eyes.

---

## 10. Shipping one

A desktop application packages the same way a service does — see
[`docs/shipping_applications.md`](shipping_applications.md) — with one
difference that matters: a GUI build **cannot** drop `GIR`/`GIO`, and the
package must depend on `libgtk-4-1` and `gir1.2-gtk-4.0`. That is a heavier
dependency than a server application carries, and it is worth deciding
deliberately: a local web UI on loopback avoids it entirely, which is the
choice several gBASIC applications have made.
