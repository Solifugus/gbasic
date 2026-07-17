# gi.* examples (GObject-Introspection bridge)

Programs written against the raw `gi.*` GObject-Introspection bridge (see
PLAN.md "Phase GI"). These are **manual** demos — like `examples/gui/`, they are
not in the golden suite.

## `gtk4_hello.bas` — GTK4 in the canonical idiom on gi.*

A window, a label + a button, a `clicked` signal, and the application's own main
loop, done through `gi.new` / `gi.set` / `gi.call` / `gi.connect` / `gi.enum`.

Requires a GTK4 runtime, its typelib, and a display:

```sh
sudo apt-get install gir1.2-gtk-4.0 libgtk-4-1      # Debian/Ubuntu
./gbasic examples/gi/gtk4_hello.bas
```

This is the **manual acceptance test** for the two follow-up bridge items. It uses
the real `GtkApplication` idiom, not the earlier v1 workarounds:

- **Construct-time properties.** The window is a genuine `Gtk.ApplicationWindow`
  built with its construct-only `application` property —
  `gi.new("Gtk.ApplicationWindow", "application", app)` — set at construction, the
  only place GTK allows it. (Earlier drafts fell back to a plain `Gtk.Window` +
  `add_window`.)
- **The application drives its own loop.** `gi.call(app, "run", 0, nothing)`:
  `run` emits `startup` (which runs `gtk_init` for us) and then `activate`, and
  blocks until the last window closes. The `argv` array is passed as `nothing`,
  which the bridge marshals to a NULL pointer (argc `0`). No separate
  `register()` / `gi.main()` dance, and no explicit `gi.quit` — closing the last
  window quits `run` on its own.

The window is created inside the `activate` handler, per the `GApplication`
contract. The `clicked` handler mutates its own `source` button, so it needs no
cross-scope widget reference.

For a namespace-level free function with no receiver (e.g. `Gtk.init`,
`GLib.markup_escape_text`), use `gi.invoke("Namespace.function", args...)` — see
`tests/gi/gi_invoke_test.bas`.

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
