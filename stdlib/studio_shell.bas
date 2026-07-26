' studio_shell.bas — the gBASIC Studio application shell (GTK 4, over gtk.bas).
'
' STU-1/STU-2 scope: a usable NAVIGATION + EDITING shell — a filesystem project
' browser tree (left) and a notebook of source-editor tabs for the open documents
' (right), plus a status bar. It is a pure VIEW over the app model studio.bas owns
' (the workspace navigation model and the document manager app.dm): it reads the
' model (and scans the filesystem via studio_browser) to populate itself and holds
' no document state of its own. Interactive wiring (browser row -> open, editor edit
' -> document manager) is owned by the entry program's handlers over a global app
' record, so the callback-scope rules are respected.
'
' Requires gi + gtk + studio_browser + studio_docs + sourceeditor loaded and GTK
' initialized, so it is only used in the display modes; the headless lifecycle and
' tests never touch it.
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

        ' --- main split: project browser | editor tab notebook ---
        split = gtk.paned("h")
        split.vexpand = true

        nav = gtk.listbox()
        studio_shell._fill_nav(nav, app)
        nav_scroll = gtk.scrolled(nav)
        split.set_start_child(nav_scroll)
        split.position = 260

        book = studio_shell._editor_tabs(app)
        split.set_end_child(book)

        outer.append(split)

        ' --- status bar ---
        status = gtk.label(studio_shell.status_text(app))
        outer.append(status)

        win.set_child(outer)
        win.present()

        return { window: win, status: status, nav: nav, notebook: book }
    end function

    ' Build the editor-tab notebook from the document manager. One page per open
    ' document: a SourceEditor over its content with gBASIC highlighting; the tab
    ' label carries a dirty (*) / missing (!) marker. The active document's page is
    ' selected. (A view is a mirror of a document — the manager stays authoritative.)
    function _editor_tabs(app)
        book = gtk.notebook()
        dm = app.dm
        if count(dm.docs) = 0 then
            book.append_page(gtk.label("(no document open)"), gtk.label("Welcome"))
            return book
        end if
        activeidx = 0
        i = 0
        for each d in dm.docs
            ed = sourceeditor.create()
            ed.set_text(d.content)
            ed.set_language("gbasic")
            view = ed.view()
            sc = gtk.scrolled(view)
            sc.vexpand = true
            sc.hexpand = true
            book.append_page(sc, gtk.label(studio_shell.tab_label(d)))
            if d.id = dm.active then
                activeidx = i
            end if
            i = i + 1
        end for
        book.set_current_page(activeidx)
        return book
    end function

    ' A tab label with markers: "! " missing, "* " dirty (unsaved), then the name.
    function tab_label(doc)
        marker = ""
        if doc.missing then
            marker = "! "
        else
            if studio_docs.is_dirty(doc) then
                marker = "* "
            end if
        end if
        return marker + doc.display_name
    end function

    function status_text(app)
        ws = app.model.workspace
        base = "ready"
        if ws != nothing then
            base = "ready — " + ws.name + " — " + count(ws.projects) + " project(s)"
        end if
        n = count(app.dm.docs)
        return base + " — " + n + " open"
    end function

end library
