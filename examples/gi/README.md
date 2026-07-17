# gi.* examples (GObject-Introspection bridge)

Programs written against the raw `gi.*` GObject-Introspection bridge (see
PLAN.md "Phase GI"). These are **manual** demos — like `examples/gui/`, they are
not in the golden suite.

## `gtk4_hello.bas` — first GTK4 program on gi.*

A window, a label + a button, a `clicked` signal, and the main loop, done purely
through `gi.new` / `gi.set` / `gi.call` / `gi.connect` / `gi.enum` / `gi.main`.

Requires a GTK4 runtime, its typelib, and a display:

```sh
sudo apt-get install gir1.2-gtk-4.0 libgtk-4-1      # Debian/Ubuntu
./gbasic examples/gi/gtk4_hello.bas
```

**Status: runs.** Verified on GTK4 4.22 with a display — opens the window and
enters the event loop with no GTK criticals. (Finding a bug on the way: `gi.call`
now walks parent classes, so inherited methods like `Gio.Application.register`
resolve; regression: `tests/gi/gi_inherited_method_test.bas`.)

It leans on three v1-bridge workarounds (all tracked as deferred items in
PLAN.md):

1. **`gtk_init` has no direct call path** (no namespace-function support yet), so
   GTK is initialised by `gi.call(app, "register", null)` — registering a
   `Gtk.Application` drives GApplication's `startup`, whose GtkApplication handler
   calls `gtk_init()`.
2. **`gi.new` can't set construct-only properties yet**, so we use a plain
   `Gtk.Window` + `gi.call(app, "add_window", win)` instead of
   `Gtk.ApplicationWindow` (whose `application` is construct-only).
3. **`GApplication.run` needs an `argv` array** the bridge can't marshal, so the
   program drives its own loop with `gi.main()` / `gi.quit()`.

If a real run shows `register` does not initialise GTK, the fix is the deferred
"namespace/global function calls" bridge item (to call `Gtk.init` directly) or
`GApplication.run` argv marshalling — noted in PLAN.md.

## `calculator.bas` — a working GTK4 calculator

A 4×4 keypad calculator that pushes more of the bridge:

- **`Gtk.Grid`** laid out with 16 **multi-int-argument method calls**
  (`grid.attach(button, col, row, w, h)`).
- **`Gtk.Entry`** display with string / float (`xalign`) / boolean (`hexpand`)
  property sets.
- **One shared `clicked` handler** wired to all 16 buttons; it reads each button's
  own `label` via `gi.get` to decide what to do.
- **State kept in the widget**: the running expression lives in the entry's `text`
  property (a gBASIC global can't be mutated from inside a handler), read/written
  with `gi.get`/`gi.set`.

Arithmetic is strictly **left-to-right** (no precedence): `2+3*4 = 20`. The
evaluator (`eval_expr`/`apply_op`) is plain gBASIC and was verified headlessly;
the full GUI was verified to launch and run the loop with no GTK criticals.

```sh
./gbasic examples/gi/calculator.bas
```

Note two gBASIC surface quirks this program documents by example: `mid`'s start
index is **0-based**, and assigning a gBASIC global inside a function only shadows
it locally (hence keeping state in the widget).
