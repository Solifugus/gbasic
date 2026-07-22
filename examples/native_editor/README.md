# native_editor — a reusable SourceEditor demo (NAP-7)

`editor_demo.bas` is a small, general GTK 4 application that embeds a
GtkSourceView-based source editor built entirely from the reusable gBASIC
libraries `stdlib/gtk.bas` and `stdlib/sourceeditor.bas`, over the generic `gi`
bridge. **It is not gBASIC Studio** and contains no Studio concepts (no projects,
execution boundaries, branches, agent, or inspector) — it is the smallest proof
that an ordinary gBASIC application can embed a real source editor.

## Run it

Needs the GTK 4 and GtkSource 5 introspection typelibs
(`gir1.2-gtk-4.0`, `gir1.2-gtksource-5`) and a display:

```sh
GBASIC_PATH=stdlib ./gbasic examples/native_editor/editor_demo.bas
```

(After `make install`, `GBASIC_PATH` is unnecessary — the installed stdlib and
its `gtksourceview/gbasic.lang` are found on the default path.)

## What it demonstrates

- a GTK 4 `GtkApplication` + `GtkApplicationWindow` (via `gtk.bas`)
- a `GtkSourceView` editor with **gBASIC syntax highlighting** (`gbasic.lang`)
- editable gBASIC source text (`set_text`/`get_text`)
- a generic **source mark** at a line (`ed.mark(line, category)`)
- a temporary **range highlight** (`ed.highlight(start, end, color)`)
- an **inline anchored widget** at a source line
  (`ed.add_inline(line, widget)` via `GtkTextChildAnchor`)
- **cursor navigation** and **scrolling** (`ed.set_cursor`, `ed.scroll_to`)

## Manual display acceptance checklist

The automated suite `tests/run_native_editor.sh` covers the headless surface
(and, when a display is present, a scripted `view`/`scroll`/inline-widget smoke).
The following is the human acceptance procedure for the full interactive editor:

1. **Open** — run the command above; a window titled "gBASIC SourceEditor"
   appears with the sample program shown.
2. **Highlighting** — keywords (`print`, `for`, `in`, `end`), the string
   `"hello"`, the comment `' edit me`, and the numbers `1 2 3` are coloured.
3. **Type** — click in the editor and type; new keywords/strings highlight live.
4. **Undo** — press Ctrl+Z; the edit is undone (GtkSourceView's built-in undo).
5. **Range highlight** — lines 3–4 have a pale-yellow background (the demo's
   `ed.highlight(2, 3, ...)`).
6. **Gutter mark** — line numbers are shown; a source mark sits on line 2.
7. **Inline widget** — a small "run" button is anchored inline at the first
   line, and the editor remains editable around it.
8. **Scroll to a location** — the view is scrolled to the top and the cursor is
   on line 2 at startup.

Record the outcome of this checklist in the phase report; it is not part of the
golden CI suite (which cannot assert pixels).
