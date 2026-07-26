' studio.bas — the gBASIC Studio application object and lifecycle (headless).
'
' This is the backbone STU-0 delivers: a deterministic STARTUP pipeline that
' assembles the authoritative Studio model from disk, and a SHUTDOWN pipeline that
' persists it atomically. It is intentionally free of GTK — the shell (studio_shell
' / examples/studio/studio.bas) is a VIEW bound to the model this layer owns, never
' a second copy of the state.
'
' Layering:   studio (lifecycle)  ->  studio_model (rules)  ->  studio_store (I/O)
'                                                          ->  studio_json (safe parse)
' Every store is versioned and read defensively: missing / corrupt / future-version
' inputs recover to defaults with a recorded diagnostic, never a crash.
'
' Requires studio_json, studio_store, and studio_model to be loaded by the program
' (loads live inside the `program` block).
library studio

    ' ---- paths -------------------------------------------------------------

    ' Resolve every store path under a single config `home` directory. Tests point
    ' `home` at a throwaway directory; a real launch would use a per-user location.
    function paths(home)
        return {
            home: home,
            settings_file: home + "/settings.json",
            session_file: home + "/session.json",
            workspaces_dir: home + "/workspaces"
        }
    end function

    function workspace_path(app, id)
        return app.paths.workspaces_dir + "/" + id + ".json"
    end function

    ' ---- future hooks (empty placeholders for later phases) ----------------

    ' Later Studio phases attach real behavior to these managers (editor: STU-2;
    ' section engine: STU-3; replay: STU-4; agent: STU-6). STU-0 only reserves the
    ' extension points so the app object has a stable place to grow, and marks them
    ' not-ready. They are LIVE hooks, never persisted with the model.
    function managers()
        return {
            editor: { kind: "editor", ready: false },
            section: { kind: "section", ready: false },
            replay: { kind: "replay", ready: false },
            agent: { kind: "agent", ready: false }
        }
    end function

    ' ---- load policy -------------------------------------------------------

    ' Turn a studio_store.read_status result into a value + a policy code:
    '   missing -> default (code "default")
    '   corrupt -> default (code "corrupt-recovered")
    '   loaded but future schema_version -> default (code "future-version-rejected")
    '   loaded and understood -> the value (code "loaded"; caller normalizes)
    function _policy(st, default_value)
        if st.status = "missing" then
            return { value: default_value, code: "default" }
        end if
        if st.status = "corrupt" then
            return { value: default_value, code: "corrupt-recovered" }
        end if
        future = studio_model.is_future(st.value)
        if future then
            return { value: default_value, code: "future-version-rejected" }
        end if
        return { value: st.value, code: "loaded" }
    end function

    ' ---- startup pipeline --------------------------------------------------

    ' main -> load global settings -> load previous session -> load its workspace
    '      -> construct the Studio model -> return the application object.
    ' Never raises on bad stored state: each store recovers to a default and
    ' appends a diagnostic. The result is the authoritative app object; the UI is
    ' built from it afterwards (studio_shell), keeping persistence and widgets apart.
    function startup(home)
        p = studio.paths(home)
        studio_store.ensure_dir(p.home)
        studio_store.ensure_dir(p.workspaces_dir)

        diagnostics = []

        ' settings
        ss = studio_store.read_status(p.settings_file)
        sp = studio._policy(ss, studio_model.default_settings())
        settings = sp.value
        if sp.code = "loaded" then
            settings = studio_model.normalize_settings(settings)
        end if
        diagnostics = append(diagnostics, "settings:" + sp.code)

        ' session
        es = studio_store.read_status(p.session_file)
        ep = studio._policy(es, studio_model.default_session())
        session = ep.value
        if ep.code = "loaded" then
            session = studio_model.normalize_session(session)
        end if
        diagnostics = append(diagnostics, "session:" + ep.code)

        ' workspace (only if the session names one and restoring is enabled)
        workspace = nothing
        want_ws = session.active_workspace
        if want_ws != "" then
            if settings.restore_last_session then
                wpath = p.workspaces_dir + "/" + want_ws + ".json"
                ws_status = studio_store.read_status(wpath)
                wp = studio._policy(ws_status, nothing)
                if wp.code = "loaded" then
                    workspace = studio_model.normalize_workspace(wp.value)
                    diagnostics = append(diagnostics, "workspace:loaded")
                else
                    ' missing/corrupt/future workspace: degrade to no workspace
                    ' (files+layout still restored), flag it, keep the session.
                    diagnostics = append(diagnostics, "workspace:" + wp.code)
                end if
            else
                diagnostics = append(diagnostics, "workspace:restore-disabled")
            end if
        else
            diagnostics = append(diagnostics, "workspace:none")
        end if

        model = {
            schema_version: studio_model.schema_version(),
            settings: settings,
            session: session,
            workspace: workspace
        }

        return {
            home: home,
            paths: p,
            model: model,
            managers: studio.managers(),
            diagnostics: diagnostics
        }
    end function

    ' ---- workspace lifecycle on the app object -----------------------------

    ' Create a new, empty workspace, make it the session's active workspace, and
    ' install it as the app's open workspace. Returns the updated app object.
    function create_workspace(app, name)
        model = app.model
        session = model.session
        minted = studio_model.mint_workspace_id(session)
        session = minted.session
        ws = studio_model.new_workspace(minted.id, name)
        session = studio_model.set_active_workspace(session, minted.id)
        model.session = session
        model.workspace = ws
        app.model = model
        return app
    end function

    ' Replace the app's open workspace with an updated one (after model mutations).
    function set_workspace(app, ws)
        model = app.model
        model.workspace = ws
        app.model = model
        return app
    end function

    ' ---- shutdown pipeline -------------------------------------------------

    ' collect current state -> serialize settings/session/workspace -> atomic
    ' replace each -> clean. Writes are atomic (studio_store), so a crash mid-save
    ' never leaves a truncated store. Returns a diagnostics array of what was saved.
    function shutdown(app)
        p = app.paths
        model = app.model
        studio_store.ensure_dir(p.home)
        studio_store.ensure_dir(p.workspaces_dir)

        saved = []
        studio_store.write_atomic(p.settings_file, model.settings)
        saved = append(saved, "settings")
        studio_store.write_atomic(p.session_file, model.session)
        saved = append(saved, "session")

        ws = model.workspace
        if ws != nothing then
            wpath = p.workspaces_dir + "/" + ws.id + ".json"
            studio_store.write_atomic(wpath, ws)
            saved = append(saved, "workspace:" + ws.id)
        end if
        return saved
    end function

    ' ---- deterministic summary (for headless tests / diagnostics) ----------

    ' A stable, path-free textual snapshot of the model — used by the headless test
    ' modes so save/restore can be asserted byte-exact without leaking temp paths.
    function summary(app)
        model = app.model
        s = model.settings
        se = model.session
        lines = []
        lines = append(lines, "settings.theme=" + s.theme)
        lines = append(lines, "settings.restore_last_session=" + s.restore_last_session)
        lines = append(lines, "settings.recent_limit=" + s.recent_limit)
        lines = append(lines, "session.active_workspace=" + se.active_workspace)
        win = se.window
        lines = append(lines, "session.window=" + win.width + "x" + win.height + " max=" + win.maximized)
        lines = append(lines, "session.recent=" + join(se.recent_files, ","))
        ws = model.workspace
        if ws = nothing then
            lines = append(lines, "workspace=none")
        else
            lines = append(lines, "workspace=" + ws.id + ":" + ws.name)
            lines = append(lines, "workspace.active_project=" + ws.active_project)
            lines = append(lines, "projects=" + count(ws.projects))
            for each pr in ws.projects
                lines = append(lines, "  " + pr.id + " " + pr.name + " docs=" + count(pr.documents))
            end for
            lines = append(lines, "tabs.order=" + join(ws.tabs.order, ","))
            lines = append(lines, "tabs.active=" + ws.tabs.active)
        end if
        lines = append(lines, "diagnostics=" + join(app.diagnostics, ";"))
        return join(lines, "\n")
    end function

end library
