# native_ui — declarative GTK4 apps on the gtkui reconciler

General-purpose demos of `stdlib/gtkui.bas`, the dynamic declarative widget-tree
reconciler (NAP-11). These are ordinary gBASIC applications — **not** gBASIC
Studio and containing no Studio concepts (no projects, execution boundaries,
branches, agent, or persistence). They exist to show that a real, dynamic GTK 4
UI can be driven from application state entirely in gBASIC over the generic `gi`
bridge, with no native/C code.

## dynamic_list.bas

A list you can grow, shrink, and reverse. The whole UI is described as a record
tree by `render()`; every button handler mutates the app state and calls
`gtkui.update`, and the reconciler mutates the existing widgets in place —
reusing each keyed row across add/remove/reverse (identity preserved), updating
the header count as a property change, and never rebuilding the tree. It also
embeds a manually-created `GtkEntry` through the native escape hatch (`widget:`),
showing reconciled and hand-built widgets side by side.

Run it (needs a display):

```sh
GBASIC_PATH=stdlib ./gbasic examples/native_ui/dynamic_list.bas
```

Manual GUI demo; parse-checked in `tests/run_gui_parse.sh`. The reconciler's
own behavior is covered by `tests/run_gtkui.sh` (headless diff logic always; a
display smoke test under `G_DEBUG=fatal-criticals`).
