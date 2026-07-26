' studio_shell.bas — the gBASIC Studio application shell (GTK 4, over gtk.bas).
'
' STU-0 scope: JUST enough window to prove the architecture — a header/menu strip,
' an (empty) navigation pane, an (empty) editor area, and a status bar. It is a
' pure VIEW over the app model that studio.bas owns: it reads the model to label
' itself and holds no state of its own. The real editor (STU-2), section widgets
' (STU-3+), inspector, and dynamic tab strip (STU-1) replace the placeholders here.
'
' Requires gi + gtk to be loaded and GTK initialized (a running gtk.application),
' so it is only used in the display modes; the headless lifecycle never touches it.
library studio_shell

    ' Build the main window from the app model and present it. Returns a record of
    ' widget references so the caller (and later phases) can bind to them.
    function build(gtkapp, app)
        model = app.model
        ws = model.workspace

        win = gtk.application_window(gtkapp)
        title = "gBASIC Studio"
        if ws != nothing then
            title = "gBASIC Studio — " + ws.name
        end if
        win.title = title
        win.default_width = model.session.window.width
        win.default_height = model.session.window.height

        outer = gtk.box("v", 0)

        ' --- header / menu strip (placeholder; real menu is a later phase) ---
        header = gtk.box("h", 6)
        header.append(gtk.label("gBASIC Studio"))
        header.append(gtk.button("Menu"))
        outer.append(header)

        ' --- main split: navigation pane | editor area ---
        split = gtk.paned("h")
        split.vexpand = true

        nav = gtk.listbox()
        if ws = nothing then
            nav.append(gtk.label("(no workspace open)"))
        else
            for each pr in ws.projects
                nav.append(gtk.label(pr.name))
                for each d in pr.documents
                    nav.append(gtk.label("  " + d.name))
                end for
            end for
        end if
        nav_scroll = gtk.scrolled(nav)
        split.set_start_child(nav_scroll)
        split.position = 240

        ' editor area — placeholder until STU-2 mounts a SourceEditor here
        editor = gtk.label("(editor area — STU-2)")
        editor.vexpand = true
        editor.hexpand = true
        editor_scroll = gtk.scrolled(editor)
        split.set_end_child(editor_scroll)

        outer.append(split)

        ' --- status bar ---
        status_text = "ready"
        if ws != nothing then
            status_text = "ready — " + ws.name + " — " + count(ws.projects) + " project(s)"
        end if
        status = gtk.label(status_text)
        outer.append(status)

        win.set_child(outer)
        win.present()

        return { window: win, status: status, nav: nav, editor: editor }
    end function

end library
