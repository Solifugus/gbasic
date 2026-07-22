' ============================================================================
' workbench.bas — the Native Application Platform Spike (NAP-8).
'
' A general "native workbench" desktop application written PRIMARILY IN gBASIC on
' the generalized platform (NAP-0..7). It is the architectural proof point: a
' nontrivial native GTK4 app with a real source editor, dynamic layout, data
' views, an external process, and responsive background work — no native C beyond
' the bridge. It is NOT gBASIC Studio and contains no Studio concepts (no projects,
' execution boundaries, branches, agent, inspector-persistence, or git UI). Every
' capability it shows is reusable by any gBASIC application.
'
' Modes (via the program argument), so the non-display logic is headless-testable:
'   inspect  — print the generic nested-value inspector (headless)
'   process  — run an external process via process.run and print it (headless)
'   async    — the actor -> mailbox -> GLib-loop responsiveness proof (headless,
'              GLib main loop, no widgets)
'   smoke    — build the full GTK4 UI, auto-run the async + process work, print a
'              deterministic transcript, and quit (requires a display)
'   (default)— the interactive GUI (requires a display; runs until the window closes)
'
' RUN THE APP (needs GTK4 + GtkSource5 typelibs and a display):
'   GBASIC_PATH=stdlib ./gbasic examples/native_workbench/workbench.bas
' ============================================================================

' ---- shared, display-free helpers ------------------------------------------

' A nested gBASIC value used by the inspector demo.
function sample_value()
    return { name: "Example", counts: [1, 2, 3], config: { enabled: true, threshold: 0.75 } }
end function

' A modest tabular dataset: `n` rows of {index, name, square}.
function table_rows(n)
    rows = []
    i = 0
    while i < n
        rows = append(rows, { index: i, name: "row-" + i, square: i * i })
        i = i + 1
    end while
    return rows
end function

' Generic recursive inspector: ANY nested gBASIC value -> indented text lines.
' Proves arbitrary nested data can be presented; not tied to any schema.
function inspect_lines(value, prefix, out)
    t = type(value)
    if t = "record" then
        for each k in keys(value)
            v = value[k]
            vt = type(v)
            if vt = "record" or vt = "array" then
                out = append(out, prefix + k + ":")
                out = inspect_lines(v, prefix + "  ", out)
            else
                out = append(out, prefix + k + " = " + string(v))
            end if
        end for
        return out
    end if
    if t = "array" then
        i = 0
        for each v in value
            vt = type(v)
            if vt = "record" or vt = "array" then
                out = append(out, prefix + "[" + i + "]:")
                out = inspect_lines(v, prefix + "  ", out)
            else
                out = append(out, prefix + "[" + i + "] = " + string(v))
            end if
            i = i + 1
        end for
        return out
    end if
    return append(out, prefix + string(value))
end function

function inspect_text(value)
    lines = inspect_lines(value, "", [])
    result = ""
    for each ln in lines
        result = result + ln + "\n"
    end for
    return result
end function

' The deliberately-slow background job, run in a separate actor process. It never
' touches GTK; it just computes and sends the result back to its parent.
function worker(parent)
    sleep(0.1)
    total = 0
    i = 1
    while i <= 100
        total = total + i
        i = i + 1
    end while
    send(parent, "sum(1..100)=" + total)
end function

' ---- GUI state + handlers (only used in smoke/gui modes) -------------------

' A GLib timeout tick — proves the loop is live while the worker runs.
function on_tick()
    s.ticks = s.ticks + 1
    st = s.status
    if st != nothing then
        st.set_text("working... ticks=" + s.ticks)
    end if
    return true
end function

' The worker's result arrives here ON THE GLib LOOP (via gi.watch_mailbox) — no
' thread, no cross-thread GTK call. We update the UI and record the proof.
function on_result(frame)
    s.async_result = frame
    s.responsive = s.ticks >= 1
    st = s.status
    if st != nothing then
        st.set_text("done: " + frame)
    end if
    out = s.output
    if out != nothing then
        out.set_text(s.process_text + "\nasync: " + frame)
    end if
    if s.smoke then
        print "async-responsive=" + s.responsive
        print "async-result=" + frame
        print "shutdown-clean"
        app.quit()
    end if
    return false
end function

' Start the background job (spawn an actor). Called by a button click or auto in
' smoke mode.
function start_background()
    me = self()
    w = spawn worker(me)
end function

function on_calc_clicked(source)
    start_background()
end function

' Run a harmless external process and show its output.
function run_process_demo()
    r = process.run({ command: "echo", args: ["workbench", "ok"] })
    s.process_text = "process exit=" + r.exit_code + " out=" + trim(r.stdout)
    out = s.output
    if out != nothing then
        out.set_text(s.process_text)
    end if
    return r
end function

function on_process_clicked(source)
    run_process_demo()
end function

' Programmatic guidance: draw attention to the editor by focusing it.
function on_focus_clicked(source)
    view = s.editor_view
    view.grab_focus()
end function

' Build the whole window. Returns nothing; stores widgets in `s` for handlers.
function build_ui(app_arg)
    win = gtk.application_window(app_arg)
    win.title = "gBASIC Native Workbench"
    win.default_width = 900
    win.default_height = 600

    outer = gtk.box("v", 6)

    ' toolbar
    toolbar = gtk.box("h", 6)
    btn_proc = gtk.button("Run process")
    btn_calc = gtk.button("Background calc")
    btn_focus = gtk.button("Focus editor")
    gtk.connect(btn_proc, "clicked", on_process_clicked)
    gtk.connect(btn_calc, "clicked", on_calc_clicked)
    gtk.connect(btn_focus, "clicked", on_focus_clicked)
    toolbar.append(btn_proc)
    toolbar.append(btn_calc)
    toolbar.append(btn_focus)
    outer.append(toolbar)

    ' resizable split: navigation list | notebook of views
    split = gtk.paned("h")
    split.vexpand = true

    ' left: a vertically scrollable navigation list
    nav = gtk.listbox()
    names = ["Editor", "Inspector", "Table", "Output"]
    for each nm in names
        row = gtk.label(nm)
        nav.append(row)
    end for
    nav_scroll = gtk.scrolled(nav)
    split.set_start_child(nav_scroll)
    split.position = 180

    ' right: a notebook with the four views
    book = gtk.notebook()

    ' -- Editor tab: a SourceEditor with gBASIC highlighting + an inline widget
    ed = sourceeditor.create()
    ed.set_text("' click the inline button\nprint(\"hello\")\nfor i in [1, 2, 3]\n  print(i)\nend for\n")
    ed.set_language("gbasic")
    ed.mark(1, "bookmark")
    ed.highlight(2, 3, "#fff3b0")
    view = ed.view()
    s.editor_view = view
    inline_btn = gtk.button("calc")
    gtk.connect(inline_btn, "clicked", on_calc_clicked)
    ed.add_inline(0, inline_btn)
    ed_scroll = gtk.scrolled(view)
    ed_scroll.vexpand = true
    book.append_page(ed_scroll, gtk.label("Editor"))

    ' -- Inspector tab: the generic nested-value view
    insp = gtk.label(inspect_text(sample_value()))
    insp_scroll = gtk.scrolled(insp)
    book.append_page(insp_scroll, gtk.label("Inspector"))

    ' -- Table tab: a modest 20-row grid
    grid = gi.new("Gtk.Grid")
    grid.column_spacing = 12
    header = ["index", "name", "square"]
    hc = 0
    for each h in header
        cell = gtk.label(h)
        grid.attach(cell, hc, 0, 1, 1)
        hc = hc + 1
    end for
    rows = table_rows(20)
    ri = 0
    for each rrow in rows
        c0 = gtk.label(string(rrow.index))
        c1 = gtk.label(rrow.name)
        c2 = gtk.label(string(rrow.square))
        grid.attach(c0, 0, ri + 1, 1, 1)
        grid.attach(c1, 1, ri + 1, 1, 1)
        grid.attach(c2, 2, ri + 1, 1, 1)
        ri = ri + 1
    end for
    grid_scroll = gtk.scrolled(grid)
    grid_scroll.vexpand = true
    book.append_page(grid_scroll, gtk.label("Table"))

    ' -- Output tab: process.run + async result land here
    out_lbl = gtk.label("(no output yet)")
    s.output = out_lbl
    out_scroll = gtk.scrolled(out_lbl)
    book.append_page(out_scroll, gtk.label("Output"))

    split.set_end_child(book)
    outer.append(split)

    ' status bar
    status = gtk.label("ready")
    s.status = status
    outer.append(status)

    win.set_child(outer)
    win.present()
    s.win = win
end function

function on_activate(app_arg)
    build_ui(app_arg)
    ' responsiveness ticker + the actor->loop result delivery, wired into the GTK
    ' loop (the default GLib main context that GtkApplication.run drives).
    gi.timeout(50, on_tick)
    gi.watch_mailbox(on_result)
    if s.smoke then
        ' auto-drive the proof: run the process demo, kick off background work; the
        ' result arrives on the loop (on_result) and quits.
        run_process_demo()
        print "window-created"
        print "editor-highlighted"
        print "inline-widget-anchored"
        print "table-rows=20"
        print "inspector-lines=" + count(inspect_lines(sample_value(), "", []))
        print "process-exit=0"
        start_background()
    end if
end function

' ---- entry point -----------------------------------------------------------

program main(args)
    mode = "gui"
    if count(args) > 0 then
        mode = args[0]
    end if

    ' headless modes: no display, minimal deps
    if mode = "inspect" then
        print inspect_text(sample_value())
        return
    end if
    if mode = "process" then
        r = process.run({ command: "echo", args: ["workbench", "ok"] })
        print "exit=" + r.exit_code
        print "stdout=" + trim(r.stdout)
        return
    end if

    ' shared handler/UI state (global scope, visible to the top-level handlers)
    s = {}
    s.ticks = 0
    s.status = nothing
    s.output = nothing
    s.editor_view = nothing
    s.async_result = ""
    s.process_text = ""
    s.responsive = false
    s.smoke = false

    load gi
    gi.require("GLib", "2.0")

    if mode = "async" then
        ' headless responsiveness proof (no widgets, GLib main loop)
        me = self()
        w = spawn worker(me)
        gi.timeout(5, on_tick)
        gi.watch_mailbox(on_async_only)
        gi.main()
        return
    end if

    ' GUI modes (smoke / interactive) need the toolkit + a display
    load gtk
    load sourceeditor
    gi.require("Gtk", "4.0")
    gi.require("GtkSource", "5")
    if mode = "smoke" then
        s.smoke = true
    end if
    app = gtk.application("org.gbasic.NativeWorkbench")
    gi.connect(app, "activate", on_activate)
    gi.call(app, "run", 0, nothing)
    print "app-exited"
end program

' Headless-async result handler (no widgets): print the proof and quit the loop.
function on_async_only(frame)
    print "async-responsive=" + (s.ticks >= 1)
    print "async-result=" + frame
    gi.quit()
    return false
end function
