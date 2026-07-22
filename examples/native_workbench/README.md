# native_workbench — the Native Application Platform Spike (NAP-8)

`workbench.bas` is a single integrated GTK 4 desktop application written
**primarily in gBASIC** on the generalized native platform (NAP-0..7). It is the
architectural **proof point**: it demonstrates that a nontrivial native app with a
real source editor, dynamic layout, data views, an external process, and
responsive background work can be built with **no native C code beyond the bridge**.

**It is not gBASIC Studio.** It contains no Studio concepts — no projects,
execution boundaries, branches, agent, variable-inspector persistence, or git UI.
Every capability it shows is reusable by any gBASIC application.

## Platform capabilities demonstrated

| # | Capability | How |
|---|------------|-----|
| 1 | GTK4 app lifecycle + native window | `gtk.application` + `activate` + `run` |
| 2 | reusable `gtk.bas` / `sourceeditor.bas` | all widgets built through them |
| 3 | GtkSourceView editor + gBASIC highlighting | `sourceeditor.create()` + `set_language("gbasic")` |
| 4 | resizable split panes | `gtk.paned("h")` |
| 5 | multiple views / tabs | `gtk.notebook` (Editor / Inspector / Table / Output) |
| 6 | vertically scrollable navigation list | `gtk.listbox` in `gtk.scrolled` |
| 7 | generic structured-value inspection | `inspect_text()` on a nested value |
| 8 | modest table | a `Gtk.Grid`, 20 rows × 3 columns |
| 9 | inline widget anchored in source | `ed.add_inline(line, gtk.button(...))` |
| 10 | background work via actor process | `spawn worker(self())` |
| 11 | event-loop delivery | `gi.watch_mailbox(on_result)` |
| 12 | UI stays responsive during work | `gi.timeout(...)` ticks while the worker sleeps |
| 13 | `process.run` | runs `echo`, shows output |
| 14 | programmatic guidance | "Focus editor" → `view.grab_focus()` |

## Run it

Needs the GTK 4 + GtkSource 5 typelibs and a display:

```sh
GBASIC_PATH=stdlib ./gbasic examples/native_workbench/workbench.bas
```

## Modes (headless-testable design)

The program dispatches on its first argument so the non-display logic is verified
without a display:

- `inspect` — print the generic nested-value inspector (headless)
- `process` — run `process.run` and print the result (headless)
- `async` — the actor → mailbox fd → GLib-loop responsiveness proof (headless GLib
  main loop, no widgets)
- `smoke` — build the full UI, auto-run the process + background work, print a
  deterministic transcript, and quit (**requires a display**)
- *(no argument)* — the interactive GUI (**requires a display**; runs until the
  window closes)

`tests/run_native_workbench.sh` runs `inspect`/`process` always, `async` when
libgirepository is present, and `smoke` when the GTK4/GtkSource typelibs and a
display are present (skipping cleanly otherwise).

## Async proof (the load-bearing one)

On the background action the app `spawn`s a **separate actor process** that sleeps
and computes, then `send`s its result to the parent. The parent's actor inbox fd is
wired into the GLib main loop via `gi.watch_mailbox`, so `on_result(frame)` fires
**on the loop thread** when the message arrives — no interpreter threads, no
cross-thread GTK calls. Meanwhile a `gi.timeout` ticker keeps firing, proving the UI
never froze. Both run inside the same `GtkApplication.run()` loop.

## Which parts are headless vs display

- **Headless-testable**: the value inspector, `process.run`, the async
  responsiveness proof (GLib loop, no widgets), and (via NAP-7) the SourceEditor
  buffer surface.
- **Display-required**: the actual window, panes, notebook, list, grid, editor
  *view*, and the inline anchored widget.

## Manual display acceptance checklist

Run without an argument (`… workbench.bas`) and confirm:

1. **Window** opens titled "gBASIC Native Workbench" with a toolbar, a split view,
   and a status line.
2. **Split** — drag the divider; the navigation list and the notebook resize.
3. **Tabs** — switch between Editor / Inspector / Table / Output.
4. **Editor** — the sample program is gBASIC-highlighted; a source mark sits on
   line 2, lines 3–4 have a pale highlight, and a "calc" button is anchored inline
   at the top. The editor is editable around it.
5. **Inspector** tab shows the nested value as indented text.
6. **Table** tab shows a 20-row, 3-column grid.
7. **Background calc** (toolbar or the inline "calc" button) — the status line
   keeps updating ("working… ticks=N") while the worker runs, then shows the
   result. The UI never freezes.
8. **Run process** — the Output tab shows `process exit=0 out=workbench ok`.
9. **Focus editor** — moves keyboard focus into the editor.
10. Closing the window exits cleanly (no GLib criticals).
