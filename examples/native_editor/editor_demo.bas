' ============================================================================
' editor_demo.bas — a general GTK4 source-editor application, built entirely from
' the reusable gtk.bas + sourceeditor.bas libraries over the generic `gi` bridge.
'
' This is NOT gBASIC Studio and contains no Studio concepts (no projects, no
' execution boundaries, no branches, no agent, no inspector). It is the smallest
' proof that an ordinary gBASIC application can embed a real GtkSourceView editor
' with gBASIC syntax highlighting, a source mark, a range highlight, cursor
' navigation, and an inline anchored widget — using only reusable libraries.
'
' Compare examples/gi/gtk4_hello.bas (raw gi.set/gi.call) to see how much the
' gtk.bas conveniences + NAP-5 `.property`/`.method()` sugar reduce the noise.
'
' RUN IT (needs the GTK4 + GtkSource5 typelibs and a display):
'   GBASIC_PATH=stdlib ./gbasic examples/native_editor/editor_demo.bas
' Manual GUI demo; not part of the golden suite (parse-only in run_gui_parse.sh).
' ============================================================================

load gi
load gtk
load sourceeditor

function on_activate(app)
    win = gtk.application_window(app)
    win.title = "gBASIC SourceEditor"
    win.default_width = 640
    win.default_height = 420

    ' The reusable editor: a GtkSourceView with gBASIC highlighting.
    ed = sourceeditor.create()
    ed.set_text("' edit me\nprint(\"hello\")\nfor i in [1, 2, 3]\n  print(i)\nend for\n")
    ed.set_language("gbasic")

    ' A generic source mark, a temporary range highlight, and an inline widget
    ' anchored at a source line — all generic capabilities, no fixed meaning.
    ed.mark(1, "bookmark")
    ed.highlight(2, 3, "#fff3b0")
    lens = gtk.button("run")
    ed.add_inline(0, lens)

    view = ed.view()
    scr = gtk.scrolled(view)
    scr.vexpand = true
    scr.hexpand = true

    win.set_child(scr)
    win.present()
    ed.set_cursor(1, 0)
    ed.scroll_to(0)
end function

app = gtk.application("org.gbasic.NativeEditorDemo")
gi.connect(app, "activate", on_activate)
gi.call(app, "run", 0, nothing)
print("done")
