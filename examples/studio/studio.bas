' ============================================================================
' studio.bas — gBASIC Studio entry point (STU-0 skeleton).
'
' STU-0 delivers the persistent BACKBONE, not an editor: launch Studio, create /
' open a workspace, close, relaunch, and find the working context restored. The
' domain model + persistence live in the stdlib studio_* libraries (headless and
' fully testable); this program is the thin entry point that runs the lifecycle
' and, in the display modes, mounts the shell view over the model.
'
' Modes (first program argument), so the backbone is headless-testable:
'   startup   <home>       — run the startup pipeline, print the model summary
'   build     <home>       — startup, build a canned workspace, shut down (persist)
'   roundtrip <home>       — build + shut down + relaunch; print restored summary
'   stress    <home>       — repeated atomic save/reload; assert every reload loads
'   cycles    <home>       — 50 startup/shutdown cycles (memory/leak probe)
'   smoke                  — build the GTK shell over a canned workspace, quit
'   (default) gui          — startup + shell + run the GTK loop (needs a display)
'
' RUN (needs GTK4 typelib + a display for gui/smoke):
'   GBASIC_PATH=stdlib ./gbasic examples/studio/studio.bas gui ~/.gbasic-studio
' ============================================================================

' Build a fixed, deterministic workspace (stable ids from the counters) so the
' save/restore and shell modes have identical, assertable state every run.
function build_canned(app)
    app = studio.create_workspace(app, "member-analytics")
    ws = app.model.workspace
    ws = studio_model.add_project(ws, "Analytics", "/home/u/analytics")
    ws = studio_model.open_document(ws, "proj-1", "/home/u/analytics/load.bas")
    ws = studio_model.open_document(ws, "proj-1", "/home/u/analytics/report.bas")
    app = studio.set_workspace(app, ws)

    session = app.model.session
    session = studio_model.set_window(session, 1024, 768, true)
    session = studio_model.touch_recent(session, "/home/u/analytics/load.bas", 10)
    m = app.model
    m.session = session
    app.model = m
    return app
end function

' GTK activate handler (display modes only). Reads the global G assembled in main.
function on_activate(gtkapp)
    shell = studio_shell.build(gtkapp, G.app)
    G.shell = shell
    if G.smoke then
        ws = G.app.model.workspace
        print "shell-built"
        if ws = nothing then
            print "workspace=none"
        else
            print "workspace=" + ws.name
            print "projects=" + count(ws.projects)
        end if
        gtkapp.quit()
    end if
end function

program main(args)
    ' Core backbone libraries — always available (loads live inside the program
    ' block; a top-level load does not run when a program block is present).
    load studio_json
    load studio_store
    load studio_model
    load studio

    mode = "gui"
    if count(args) > 0 then
        mode = args[0]
    end if

    home = ".gbasic-studio"
    if count(args) > 1 then
        home = args[1]
    end if

    ' ---- headless lifecycle modes -----------------------------------------

    if mode = "startup" then
        app = studio.startup(home)
        print studio.summary(app)
        return
    end if

    if mode = "build" then
        app = studio.startup(home)
        app = build_canned(app)
        saved = studio.shutdown(app)
        print "saved=" + join(saved, ",")
        return
    end if

    if mode = "roundtrip" then
        app = studio.startup(home)
        app = build_canned(app)
        studio.shutdown(app)
        app2 = studio.startup(home)
        print studio.summary(app2)
        return
    end if

    if mode = "stress" then
        app = studio.startup(home)
        app = build_canned(app)
        i = 0
        ok = true
        while i < 30
            studio.shutdown(app)
            chk = studio.startup(home)
            w = chk.model.workspace
            if w = nothing then
                ok = false
            end if
            i = i + 1
        end while
        print "stress_ok=" + ok
        return
    end if

    if mode = "cycles" then
        i = 0
        while i < 50
            a = studio.startup(home)
            studio.shutdown(a)
            i = i + 1
        end while
        print "cycles_done=50"
        return
    end if

    ' ---- display modes (need GTK4 + a display) ----------------------------

    G = {}
    G.app = studio.startup(home)
    G.smoke = false
    G.shell = nothing
    if mode = "smoke" then
        G.smoke = true
        G.app = build_canned(G.app)
    end if

    load gi
    load gtk
    load studio_shell
    gi.require("Gtk", "4.0")

    gtkapp = gtk.application("org.gbasic.Studio")
    gi.connect(gtkapp, "activate", on_activate)
    gi.call(gtkapp, "run", 0, nothing)
    print "app-exited"
end program
