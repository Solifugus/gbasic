' studio_session — STU-4 execution sessions (headless).
'
' Runs an execution section by REPLAY: running section N means executing sections
' 1..N in a fresh child interpreter. R2 (docs/gbasic_studio_r2.md) established that
' this is the only model the runtime supports -- there is no reusable evaluation
' context, and Studio (being gBASIC) could not reach one if there were.
'
' Studio owns the session; the platform owns the child. The child is driven with
' PLAT-PROC's non-blocking primitives (process.start/poll/read/stop/release)
' directly from a GTK timeout callback -- no actor, no mailbox (R2 amendment
' 2026-07-27).
'
' A session belongs to ONE document and permits at most one active run. Sessions
' hold no shared mutable state, so different documents run concurrently for free.
'
' STATE MACHINE (every state explicit; no boolean soup):
'   idle          nothing has run yet, or the last run was cleared
'   materializing writing the prefix file
'   running       child launched, being polled
'   stopping      SIGTERM sent, waiting for the child to go
'   unresponsive  SIGTERM sent, the grace window elapsed, child still alive
'   finished      the child exited (of its own accord or after a stop)
'   failed        the run could not proceed (materialize or launch error)
'   refused       Studio declined to start (see can_run)
'
' TRANSITIONS (all named, all tested):
'   idle|finished|failed|refused -> materializing   run requested and permitted
'   idle|finished|failed|refused -> refused         run requested and refused
'   materializing -> running                        child launched
'   materializing -> failed                         write or launch error
'   running -> running                              tick with the child alive
'   running -> finished                             child exited
'   running -> stopping                             stop requested
'   stopping -> finished                            child died
'   stopping -> unresponsive                        grace elapsed, still alive
'   unresponsive -> finished                        force stop killed it
'   running|stopping|unresponsive -> ... -> materializing   restart, after the stop
'
' See docs/gbasic_studio_stu4.md.

library studio_session

    function schema_version()
        return 1
    end function

    ' ---- state helpers -----------------------------------------------------

    ' Record a transition and move. Every state change goes through here, so the
    ' transition log in the goldens is the machine's actual history, not a
    ' reconstruction.
    function _to(session, next_state)
        if session.state != next_state then
            session.transitions = append(session.transitions, session.state + ">" + next_state)
        end if
        session.state = next_state
        return session
    end function

    function is_active(session)
        if session.state = "materializing" then
            return true
        end if
        if session.state = "running" then
            return true
        end if
        if session.state = "stopping" then
            return true
        end if
        if session.state = "unresponsive" then
            return true
        end if
        return false
    end function

    ' ---- construction ------------------------------------------------------

    function create(doc_id, scratch_dir)
        return {
            schema_version: 1,
            doc_id: doc_id,
            scratch_dir: scratch_dir,
            interpreter: "./gbasic",
            state: "idle",
            transitions: [],
            run_seq: 0,
            section_id: "",
            section_index: -1,
            prefix_path: "",
            prefix_bytes: 0,
            appended: "",
            handle: nothing,
            reason: "",
            message: "",
            out_prefix: "",
            out_target: "",
            err_prefix: "",
            err_target: "",
            split: "none",
            stderr_raw: "",
            diagnostics: [],
            attribution: [],
            exit_code: -1,
            signal: 0,
            success: false,
            pending: nothing,
            ' How many ticks a bare stop waits before the session reports the child
            ' as `unresponsive`. At the shell's 50ms cadence, 20 ticks is ~1s: long
            ' enough for a well-behaved child to run its SIGTERM handler and exit,
            ' short enough that the user learns quickly that force is needed.
            stop_ticks: 0,
            stop_grace_ticks: 20
        }
    end function

    ' ---- refusal -----------------------------------------------------------

    ' Whether `section_id` may be run against this section state. Refusal reasons
    ' are CODES the UI can map to its own wording; `message` is the fallback text.
    '
    ' Last-known-good sections exist so the UI stays coherent while the user is
    ' mid-edit -- NOT so stale code can be executed. Running against source that no
    ' longer parses would execute a file whose bytes do not match the sections
    ' Studio is reasoning about, so it is refused outright.
    function can_run(sections, section_id)
        if not sections.valid then
            return { ok: false, reason: "source-invalid",
                     message: "the document does not parse; fix the errors first" }
        end if
        for each sid in sections.stale_ids
            if sid = section_id then
                return { ok: false, reason: "section-stale",
                         message: "that section no longer exists in the current source" }
            end if
        end for
        found = nothing
        for each s in sections.sections
            if s.id = section_id then
                found = s
            end if
        end for
        if found = nothing then
            return { ok: false, reason: "section-missing",
                     message: "no such section in the current source" }
        end if
        if found.status = "ambiguous" then
            return { ok: false, reason: "section-ambiguous",
                     message: "that section is ambiguous after the last edit; disambiguate it first" }
        end if
        if found.status = "stale" then
            return { ok: false, reason: "section-stale",
                     message: "that section no longer exists in the current source" }
        end if
        return { ok: true, reason: "", message: "" }
    end function

    ' ---- prefix materialization -------------------------------------------

    ' The largest whole-codepoint prefix of `text` that is at most `nbytes` long.
    ' Binary search over `mid` (which counts CODEPOINTS) using byte_count as the
    ' oracle: O(log n) C-level slices rather than an interpreted per-byte loop, which
    ' would be O(n^2) because `append` copies the array every call. Section offsets
    ' always land on codepoint boundaries, so the result is exact.
    function _byte_prefix(text, nbytes)
        lo = 0
        hi = len(text)
        while lo < hi
            probe = floor((lo + hi + 1) / 2)
            if byte_count(mid(text, 0, probe)) <= nbytes then
                lo = probe
            else
                hi = probe - 1
            end if
        end while
        return mid(text, 0, lo)
    end function

    ' The source text to execute for a run of `section`.
    '
    ' APPEND ONLY. The body is the literal BYTE PREFIX of the document,
    ' source[0, section.end_offset) -- never a stitched-together subset. That is what
    ' keeps every line of the materialized file at the SAME line number as in the
    ' document, which is in turn what lets --json-diagnostics positions map straight
    ' back through STU-3's ranges to a section id. Nothing is ever inserted between
    ' or inside sections, and there are no sentinel markers.
    '
    ' Two appends are permitted, both strictly at the end, neither shifting a line
    ' of the prefix:
    '   * a trailing newline, because section ranges are newline-EXCLUSIVE, so the
    '     raw prefix ends immediately after a terminator;
    '   * `end program`, when the target section lives inside a `program` block
    '     (STU-3 records that as ancestry "program:NAME"), because a byte prefix of
    '     such a document cuts the block open.
    function materialize_text(source, section)
        body = studio_session._byte_prefix(source, section.end_offset)
        appended = ""
        n = byte_count(body)
        if n > 0 then
            if byte_at(body, n - 1) != 10 then
                body = body + "\n"
                appended = "newline"
            end if
        end if
        anc = section.anchor.ancestry
        if left(anc, 8) = "program:" then
            body = body + "end program\n"
            if appended = "" then
                appended = "end-program"
            else
                appended = appended + "+end-program"
            end if
        end if
        return { text: body, appended: appended }
    end function

    function _prefix_path(session)
        return session.scratch_dir + "/run-" + session.doc_id + "-" + session.run_seq + ".bas"
    end function

    ' ---- running -----------------------------------------------------------

    ' Start a run of `section_id`. Refusal is checked FIRST, so a refused run never
    ' touches the filesystem. Returns the updated session; inspect `.state`.
    function run(session, sections, source, section_id)
        if studio_session.is_active(session) then
            session.reason = "already-running"
            session.message = "a run is already in progress for this document"
            return session
        end if

        gate = studio_session.can_run(sections, section_id)
        if not gate.ok then
            session = studio_session._to(session, "refused")
            session.section_id = section_id
            session.reason = gate.reason
            session.message = gate.message
            return session
        end if

        ' Reset per-run results before the new run so nothing leaks across.
        session.reason = ""
        session.message = ""
        session.out_prefix = ""
        session.out_target = ""
        session.err_prefix = ""
        session.err_target = ""
        session.stderr_raw = ""
        session.diagnostics = []
        session.attribution = []
        session.exit_code = -1
        session.signal = 0
        session.success = false
        session.section_id = section_id
        session.run_seq = session.run_seq + 1

        idx = -1
        i = 0
        target = nothing
        for each s in sections.sections
            if s.id = section_id then
                idx = i
                target = s
            end if
            i = i + 1
        end for
        session.section_index = idx
        ' Section 1 has no prefix, so its output is unambiguously its own; any later
        ' section shares one output stream with the sections replayed before it.
        if idx = 0 then
            session.split = "exact"
        else
            session.split = "combined"
        end if

        session = studio_session._to(session, "materializing")

        m = studio_session.materialize_text(source, target)
        session.appended = m.appended
        session.prefix_bytes = byte_count(m.text)
        studio_store.ensure_dir(session.scratch_dir)
        path = studio_session._prefix_path(session)
        studio_store.write_text_atomic(path, m.text)
        session.prefix_path = path

        pf(file) = path
        if not exists(pf) then
            session = studio_session._to(session, "failed")
            session.reason = "materialize-failed"
            session.message = "could not write the execution prefix"
            return session
        end if

        session.handle = process.start({
            command: session.interpreter,
            args: ["--json-diagnostics", path]
        })
        session = studio_session._to(session, "running")
        return session
    end function

    ' ---- ticking -----------------------------------------------------------

    ' One non-blocking service of the child: drain whatever it has produced, learn
    ' whether it is still alive, and advance the state machine. Safe to call in any
    ' state; a no-op when no child is live. This is what the GTK timeout drives.
    function tick(session)
        if session.handle = nothing then
            return session
        end if
        if not studio_session.is_active(session) then
            return session
        end if

        c = process.read(session.handle)
        session = studio_session._absorb(session, c.stdout, c.stderr)

        s = process.poll(session.handle)
        if s.running then
            if session.state = "stopping" then
                session.stop_ticks = session.stop_ticks + 1
                if session.stop_ticks >= session.stop_grace_ticks then
                    session = studio_session._to(session, "unresponsive")
                end if
            end if
            return session
        end if

        ' The child is gone: take a final read so no tail output is lost, record the
        ' outcome, attribute the diagnostics, and release the handle.
        c = process.read(session.handle)
        session = studio_session._absorb(session, c.stdout, c.stderr)
        session.exit_code = s.exit_code
        session.signal = s.signal
        session.success = s.success
        process.release(session.handle)
        session.handle = nothing
        session = studio_session._to(session, "finished")
        session = studio_session.cleanup_prefix(session)
        return session
    end function

    ' Route freshly-read bytes into the prefix/target buckets.
    '
    ' HONEST LIMITATION. A single child produces ONE stdout stream for sections
    ' 1..N, and the APPEND-ONLY invariant forbids the sentinel between sections N-1
    ' and N that would be needed to split it. So:
    '   split = "exact"    -- the target is section 1: there is no prefix, and all
    '                         output is unambiguously the target's.
    '   split = "combined" -- the target is a later section: the stream is reported
    '                         under `prefix`, because every byte of it MAY be replay
    '                         output. Nothing is discarded and nothing is claimed to
    '                         be the target's that might not be.
    ' See docs/gbasic_studio_stu4.md for the options this leaves open to STU-5.
    function _absorb(session, out_chunk, err_chunk)
        if session.split = "exact" then
            session.out_target = session.out_target + out_chunk
        else
            session.out_prefix = session.out_prefix + out_chunk
        end if
        session.stderr_raw = session.stderr_raw + err_chunk
        return session
    end function

    ' ---- stopping ----------------------------------------------------------

    ' Bare stop: SIGTERM and nothing else. A child that ignores it stays alive and
    ' the session moves to `unresponsive` after the grace window -- surfaced as a
    ' distinct state rather than as a hang. Escalation is a separate user action.
    '
    ' Named `request_stop` rather than `stop` because `stop` is a gBASIC keyword
    ' (the STOP statement) and cannot be a function name.
    function request_stop(session)
        if not studio_session.is_active(session) then
            return session
        end if
        if session.handle = nothing then
            return session
        end if
        session.stop_ticks = 0
        session = studio_session._to(session, "stopping")
        process.stop(session.handle)
        return studio_session.tick(session)
    end function

    ' Force stop: the explicit escalation. SIGTERM, a bounded grace, then SIGKILL.
    function force_stop(session, grace_seconds)
        if not studio_session.is_active(session) then
            return session
        end if
        if session.handle = nothing then
            return session
        end if
        ' Escalating from `unresponsive` goes straight to `finished`; re-announcing
        ' `stopping` would only add a transition the machine does not really make.
        if session.state = "running" then
            session = studio_session._to(session, "stopping")
        end if
        s = process.stop(session.handle, { force_after: grace_seconds })
        c = process.read(session.handle)
        session = studio_session._absorb(session, c.stdout, c.stderr)
        session.exit_code = s.exit_code
        session.signal = s.signal
        session.success = s.success
        process.release(session.handle)
        session.handle = nothing
        session = studio_session._to(session, "finished")
        return studio_session.cleanup_prefix(session)
    end function

    ' ---- restart -----------------------------------------------------------

    ' Stop-then-run, with the stop completed FIRST so two runs of one session can
    ' never overlap. A restart requested mid-run records the intent, force-stops the
    ' current child (bounded, so a SIGTERM-ignoring child cannot wedge the restart),
    ' and only then starts the new run.
    function restart(session, sections, source, section_id, grace_seconds)
        if studio_session.is_active(session) then
            session.pending = { kind: "restart", section_id: section_id }
            session = studio_session.force_stop(session, grace_seconds)
        end if
        session.pending = nothing
        return studio_session.run(session, sections, source, section_id)
    end function

    ' ---- diagnostics & attribution ----------------------------------------

    ' Split the child's stderr into structured diagnostics and plain text. With
    ' --json-diagnostics each diagnostic is one JSON object per line, but the child's
    ' OWN stderr writes are interleaved there too, so every line is validated before
    ' decoding (`decode` raises, and a raise cannot be caught from a library).
    function parse_diagnostics(session)
        diags = []
        plain = []
        for each line in split(session.stderr_raw, "\n")
            if line = "" then
                continue
            end if
            ok = studio_json.valid(line)
            handled = false
            if ok then
                d = decode(line)
                if has(d, "severity") then
                    if has(d, "start") then
                        diags = append(diags, d)
                        handled = true
                    end if
                end if
            end if
            if not handled then
                plain = append(plain, line)
            end if
        end for
        session.diagnostics = diags
        if session.split = "exact" then
            session.err_target = join(plain, "\n")
        else
            session.err_prefix = join(plain, "\n")
        end if
        return session
    end function

    ' Map each diagnostic's 1-based line/column back to a section id through STU-3's
    ' byte ranges, and classify where it landed. The mapping is exact BECAUSE the
    ' materialized file is a literal byte prefix: line 12 of the child's file is line
    ' 12 of the document.
    '
    '   where = "target"   the error is in the section the user asked to run
    '   where = "prefix"   the error is in a replayed earlier section
    '   where = "outside"  the position is in no section at all -- a gap between
    '                      sections, or the appended `end program` line, which sits
    '                      beyond every section's range
    function attribute(session, sections, source)
        out = []
        for each d in session.diagnostics
            line = d.start.line
            col = d.start.column
            off = studio_sections.offset_of(source, line, col)
            sid = studio_sections.section_at(sections, off)
            where = "outside"
            if sid != nothing then
                if sid = session.section_id then
                    where = "target"
                else
                    where = "prefix"
                end if
            end if
            out = append(out, {
                section_id: sid,
                where: where,
                line: line,
                column: col,
                severity: d.severity,
                message: d.message
            })
        end for
        session.attribution = out
        return session
    end function

    ' Convenience: everything that must happen once a run has finished.
    function finalize(session, sections, source)
        session = studio_session.parse_diagnostics(session)
        return studio_session.attribute(session, sections, source)
    end function

    ' ---- scratch lifecycle -------------------------------------------------

    ' Delete this run's materialized prefix. Called when a run finishes; the child
    ' has long since parsed the file, so removing it cannot disturb a live run.
    function cleanup_prefix(session)
        if session.prefix_path = "" then
            return session
        end if
        f(file) = session.prefix_path
        if exists(f) then
            delete(f)
        end if
        session.prefix_path = ""
        return session
    end function

    ' Remove every materialized prefix left in the scratch directory. A Studio that
    ' crashed mid-run leaves its file behind, so this runs at startup: the scratch
    ' directory is Studio-owned and holds nothing worth keeping between launches.
    ' Returns how many files were removed.
    function sweep_scratch(scratch_dir)
        ' `exists` wants a FILE reference even when the path is a directory (same
        ' quirk studio_store.ensure_dir documents).
        probe(file) = scratch_dir
        if not exists(probe) then
            return 0
        end if
        d(dir) = scratch_dir
        removed = 0
        for each e in list(d)
            if e.type != "folder" then
                f(file) = scratch_dir + "/" + e.name
                if exists(f) then
                    delete(f)
                    removed = removed + 1
                end if
            end if
        end for
        return removed
    end function

    ' ---- summary (tests / minimal UI) --------------------------------------

    function summary(session)
        lines = []
        head = "state=" + session.state + " section=" + session.section_id + " run=" + session.run_seq + " split=" + session.split
        lines = append(lines, head)
        if session.reason != "" then
            lines = append(lines, "reason=" + session.reason + " message=" + session.message)
        end if
        if session.state = "finished" then
            lines = append(lines, "exit_code=" + session.exit_code + " signal=" + session.signal + " success=" + session.success)
        end if
        lines = append(lines, "out_prefix=<" + session.out_prefix + ">")
        lines = append(lines, "out_target=<" + session.out_target + ">")
        for each a in session.attribution
            sid = "-"
            if a.section_id != nothing then
                sid = a.section_id
            end if
            lines = append(lines, "! " + a.where + " section=" + sid + " " + a.line + ":" + a.column + " " + a.message)
        end for
        return join(lines, "\n")
    end function

    function transitions(session)
        return join(session.transitions, " ")
    end function

end library
