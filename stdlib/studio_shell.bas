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

    ' ---- STU-4: execution strip + output pane ------------------------------
    '
    ' Deliberately minimal (STU-4 scope): run / stop / force-stop, the session state,
    ' and an output pane that keeps PREFIX output visually separate from TARGET
    ' output. No results history, no inspector, no gutter work -- those are STU-5.
    '
    ' The widgets are returned rather than wired: like the rest of this shell, the
    ' entry program owns the handlers over its global app record, because a callback
    ' cannot rebind a top-level scalar.

    function run_bar()
        bar = gtk.box("h", 6)
        run_btn = gtk.button("Run Section")
        halt_btn = gtk.button("Stop")
        force_btn = gtk.button("Force Stop")
        state = gtk.label("run: idle")
        bar.append(run_btn)
        bar.append(halt_btn)
        bar.append(force_btn)
        bar.append(state)
        ' `stop` is a gBASIC keyword and cannot be a record key, hence `halt`.
        return { box: bar, run: run_btn, halt: halt_btn, force: force_btn, state: state }
    end function

    ' One line of session state for the strip: what is happening, to which section,
    ' and -- when Studio refused or the child is gone -- why.
    function session_text(session)
        if session = nothing then
            return "run: (no session)"
        end if
        line = "run: " + session.state
        if session.section_id != "" then
            line = line + " [" + session.section_id + "]"
        end if
        if session.state = "refused" then
            return line + " — " + session.message
        end if
        if session.state = "failed" then
            return line + " — " + session.message
        end if
        if session.state = "finished" then
            if session.signal != 0 then
                return line + " — killed by signal " + session.signal
            end if
            return line + " — exit " + session.exit_code
        end if
        return line
    end function

    function output_pane()
        box = gtk.box("v", 4)
        prefix_head = gtk.label("Prefix output — sections replayed before the target")
        prefix_body = gtk.label("")
        target_head = gtk.label("Target output — the section you ran")
        target_body = gtk.label("")
        box.append(prefix_head)
        box.append(prefix_body)
        box.append(target_head)
        box.append(target_body)
        return { box: box, prefix: prefix_body, target: target_body }
    end function

    ' The two panes' text. Prefix output is ALWAYS shown, never folded away: it is
    ' the only way a user can see that the replay re-issued the prefix's side
    ' effects.
    '
    ' While a run is in flight the split is not yet decided, so BOTH panes show the
    ' raw stream under the prefix heading rather than guessing at a boundary that
    ' may not have been printed yet. Once the run ends the panes show whichever
    ' answer the marker actually supports (STU-4B), and when it supports none they
    ' say so instead of pretending a boundary exists.
    function output_prefix_text(session)
        if session = nothing then
            return "(none)"
        end if
        if session.split_out = "pending" then
            if session.out_raw = "" then
                return "(running — no output yet)"
            end if
            return session.out_raw
        end if
        if session.split_out = "combined" then
            if session.out_prefix = "" then
                return "(none — sections 1..N combined)"
            end if
            return session.out_prefix
        end if
        if session.out_prefix = "" then
            return "(none)"
        end if
        return session.out_prefix
    end function

    function output_target_text(session)
        if session = nothing then
            return "(none)"
        end if
        if session.split_out = "pending" then
            return "(running — not separated yet)"
        end if
        if session.split_out = "combined" then
            return "(not separable from the prefix in this run)"
        end if
        if session.out_target = "" then
            return "(none)"
        end if
        return session.out_target
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
