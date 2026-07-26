' studio_shell.bas — the gBASIC Studio application shell (GTK 4, over gtk.bas).
'
' STU-1 scope: a usable NAVIGATION shell — a workspace label, a project list, and a
' filesystem project browser tree for the active project — plus a placeholder editor
' area and a status bar. It is a pure VIEW over the app model studio.bas owns: it
' reads the model (and scans the filesystem via studio_browser) to populate itself
' and holds no state of its own. The real editor (STU-2) replaces the placeholder;
' click-to-select / expand wiring that mutates the model is layered on next.
'
' Requires gi + gtk + studio_browser loaded and GTK initialized, so it is only used
' in the display modes; the headless lifecycle and tests never touch it.
library studio_shell

    ' Render the navigation pane contents into a listbox from the model + filesystem.
    function _fill_nav(nav, app)
        ws = app.model.workspace
        if ws = nothing then
            nav.append(gtk.label("(no workspace open)"))
            return nothing
        end if
        nav.append(gtk.label("Workspace: " + ws.name))
        for each pr in ws.projects
            marker = "  "
            if pr.id = ws.active_project then
                marker = "* "
            end if
            nav.append(gtk.label(marker + pr.name))
        end for
        ' Browser tree for the active project (files as files; no parsing).
        proj = studio_model.project_by_id(ws, ws.active_project)
        if proj = nothing then
            return nothing
        end if
        nodes = studio_browser.scan_project(proj, ws.nav.expanded)
        for each r in studio_browser.flatten(nodes)
            indent = "  "
            i = 0
            while i < r.depth
                indent = indent + "  "
                i = i + 1
            end while
            glyph = "  "
            if r.kind = "dir" then
                if r.expanded then
                    glyph = "v "
                else
                    glyph = "> "
                end if
            end if
            nav.append(gtk.label(indent + glyph + r.name))
        end for
        return nothing
    end function

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

        ' --- header / menu strip (placeholder; real actions wired in later) ---
        header = gtk.box("h", 6)
        header.append(gtk.label("gBASIC Studio"))
        header.append(gtk.button("Workspace"))
        header.append(gtk.button("New Project"))
        header.append(gtk.button("Refresh"))
        outer.append(header)

        ' --- main split: project browser | editor area ---
        split = gtk.paned("h")
        split.vexpand = true

        nav = gtk.listbox()
        studio_shell._fill_nav(nav, app)
        nav_scroll = gtk.scrolled(nav)
        split.set_start_child(nav_scroll)
        split.position = 260

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
