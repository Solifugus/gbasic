' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.

' intake.bas -- what arrived, what was taken, what to keep.
'
' The half of the ETL cycle that needs STATE. Everything else the brief behind
' docs/etl_files_design.md asked for became a PRIMITIVE, because it was a thing
' the language could not do -- streaming a file, globbing a tree, taking a path
' apart. "Have I already imported this?" is different in kind: a program could
' always answer it, and had to write the answer itself, every time, differently.
'
'   ledger = intake.open({file}"/data/.intake.json",
'                        { identity: "content", ready: "renamed" })
'
'   for each f in intake.arrived(ledger, {dir}"/data", "**/*.xlsx")
'       rows = read_vendor(f.path)
'       ledger = intake.taken(ledger, f, { rows: count(rows) })
'   end for
'
'   intake.close(ledger)
'
' THREE THINGS TO READ IN THAT, each of which is a decision with a measurement
' behind it (docs/intake_design.md §12 lists all thirteen).
'
' `intake.taken` RETURNS THE LEDGER. A gBASIC record is a VALUE, so a function
' cannot mutate its caller's copy -- the rule `accounting.post`, `lending.apply`
' and `credit.apply` all follow. A mutate-in-place API would silently do
' nothing here, and "nothing" means every file is imported again next run.
'
' `identity:` AND `ready:` ARE BOTH REQUIRED, with no default. Each declares
' something that is correct for one kind of source and WRONG for the others,
' and being wrong looks exactly like success: a bad identity re-imports or skips
' in silence, a bad readiness rule takes half a file. The rule `ldap`'s
' `security` and `lending`'s accrual basis already follow -- the argument with
' consequences may not be defaulted -- and the refusal names the choices, so the
' error teaches the decision.
'
' THE LEDGER IS A SNAPSHOT PLUS AN APPEND-ONLY JOURNAL, and that is measured
' rather than architectural taste. Rewriting the snapshot after every file is
' crash-safe, three lines, and QUADRATIC: against a ledger already holding
' 50,000 entries, taking 400 files costs 11.90s of rewriting (1.3 GB written)
' against 0.11s of appends, which is the interpreter's own start-up. So `taken`
' appends one line and `open` replays -- through `for each line in f`, so this
' library's recovery path is the streaming increment it was built on top of.
' Measured: 100,000 journal lines onto a 50,000-entry snapshot in 0.51s.
'
' EVERY TIME IS AN EPOCH NUMBER, not a datetime. Measured: strict JSON refuses
' both a `datetime` and a `duration`, which is the wall `agent`'s run hit from
' the same direction and for the same reason. The library converts at its edges.

library intake

    ' Loaded INSIDE the library block, which is not a style choice: a top-level
    ' `load` does not put the dependency in scope for this library's own
    ' functions, and the failure is INVISIBLE whenever the caller has also
    ' loaded it -- `persist.ensure_dir` was `undefined function` from in here
    ' while working perfectly from a two-line program. `grid.bas` states this
    ' above its own `load frame`, which is where I should have read it rather
    ' than measuring it again.
    load persist from "persist.bas"

    ' ---- constants ---------------------------------------------------------

    ' The ledger format this version writes and can read. A LATER version is
    ' refused rather than read optimistically: guessing at a format is how a
    ' ledger gets corrupted, and a corrupted ledger re-imports the year.
    function _version()
        return 1
    end function

    ' `identity: "content"` loads the whole file -- measured at just over 2x the
    ' file in resident memory (200 MB of file, 403 MB of process) -- so a 10 GB
    ' extract would need ~20 GB of RAM. Above this bound the hash is REFUSED by
    ' name rather than attempted, because the alternative is the host's OOM
    ' killer ending the run with no diagnostic at all. 256 MB by default;
    ' `max_hash:` overrides it for a caller who has measured their own machine.
    function _default_max_hash()
        return 268435456
    end function

    ' ---- options -----------------------------------------------------------

    function _identities()
        return ["path", "stamp", "content"]
    end function

    function _option_names()
        return ["identity", "ready", "max_hash", "rebuild"]
    end function

    ' An unknown option field is refused BY NAME -- the rule `webserver.listen`
    ' follows -- because a misspelled option that is ignored leaves the author
    ' believing they declared something they did not. `ready: { stable_for: }`
    ' is the one nested form, checked where it is read.
    function _check_options(opts)
        if type(opts) != "record" then
            error { message: "intake.open expects an options record, for example"
                             + " { identity: \"stamp\", ready: \"renamed\" }",
                    code: 1004, source: "intake" }
        end if
        for each k in keys(opts)
            if not contains(_option_names(), k) then
                error { message: "intake.open: unknown option '" + k + "'."
                                 + " Known options: " + join(_option_names(), ", "),
                        code: 1004, source: "intake" }
            end if
        next
        if not has(opts, "identity") then
            error { message: "intake.open: identity: is required, with no default."
                             + " \"path\" -- the path alone, for a source that never"
                             + " reuses a name. \"stamp\" -- path, size and modified"
                             + " time, which the listing already carries, so it costs"
                             + " nothing. \"content\" -- sha256 of the bytes, for a"
                             + " sender who re-drops the same filename with new"
                             + " contents. Guessing re-imports silently or skips"
                             + " silently, and in ETL both look like success.",
                    code: 1004, source: "intake" }
        end if
        if not contains(_identities(), opts.identity) then
            error { message: "intake.open: identity must be one of "
                             + join(_identities(), ", ") + ", not "
                             + string(opts.identity),
                    code: 1004, source: "intake" }
        end if
        ' Refused at OPEN rather than at the first file, which may be hours into
        ' a run: `sha256` is a builtin behind libcrypto, so on a lean build this
        ' library LOADS and this one mode then fails -- the third category
        ' tests/run_library_tiers.sh names, and the one a program cannot see
        ' coming.
        if opts.identity = "content" and not has_builtin("sha256") then
            error { message: "intake.open: identity \"content\" needs sha256, which"
                             + " this build of gBASIC does not have (it is behind"
                             + " libcrypto). Use identity \"stamp\", which compares"
                             + " path, size and modified time and needs nothing.",
                    code: 1004, source: "intake" }
        end if
        if not has(opts, "ready") then
            error { message: "intake.open: ready: is required, with no default."
                             + " \"renamed\" -- the writer builds the file elsewhere"
                             + " and renames it in, so a file that is visible is"
                             + " whole. \"sentinel\" -- take x only once x.done"
                             + " appears. { stable_for: 30 seconds } -- unchanged for"
                             + " that long, which is a PROBABILITY and not a"
                             + " guarantee: measured, a file's size held still across"
                             + " consecutive samples three times while it was still"
                             + " being written.",
                    code: 1004, source: "intake" }
        end if
        _check_ready(opts.ready)
        return nothing
    end function

    function _check_ready(r)
        if type(r) = "string" then
            if r != "renamed" and r != "sentinel" then
                error { message: "intake.open: ready must be \"renamed\","
                                 + " \"sentinel\" or { stable_for: <duration> }, not "
                                 + string(r),
                        code: 1004, source: "intake" }
            end if
            return nothing
        end if
        if type(r) = "record" then
            if not has(r, "stable_for") then
                error { message: "intake.open: a ready record must carry"
                                 + " stable_for:, for example"
                                 + " { stable_for: 30 seconds }",
                        code: 1004, source: "intake" }
            end if
            if type(r.stable_for) != "duration" then
                error { message: "intake.open: stable_for must be a duration, for"
                                 + " example 30 seconds or 5 minutes, not "
                                 + type(r.stable_for),
                        code: 1004, source: "intake" }
            end if
            return nothing
        end if
        error { message: "intake.open: ready must be \"renamed\", \"sentinel\" or"
                         + " { stable_for: <duration> }, not " + type(r),
                code: 1004, source: "intake" }
        return nothing
    end function

    ' ---- opening -----------------------------------------------------------

    ' The journal sits beside the snapshot, and is named from it rather than
    ' configured: two paths a caller could get independently wrong are two ways
    ' to lose a ledger's recent half.
    function _journal_path(path)
        return path + ".journal"
    end function

    function open(ledger_file, opts)
        _check_options(opts)
        path = string(ledger_file)
        persist.ensure_dir(directory_name(path))

        ' ONE RUN AT A TIME, and the lock is taken here rather than per write
        ' because the read-modify-write spans the whole run: `arrived` decides
        ' what is new from state that `taken` then changes. Measured, `lock`
        ' BLOCKS and has no non-blocking form, so a second run started while
        ' this one works WAITS for it -- stated rather than worked around, since
        ' a protocol that cannot refuse cannot be a queue. A run that dies
        ' without releasing is survivable: flock is released by the kernel.
        lockfile {file}= path + ".lock"
        if not exists(lockfile) then
            write(lockfile, "")
        end if
        lock(lockfile)

        st = persist.read_status(path)
        if st.status = "corrupt" then
            ' REFUSED rather than rebuilt, because a rebuilt ledger is an empty
            ' one and an empty one re-imports everything the feed has ever sent.
            ' The caller may say otherwise, and has to say it.
            if has(opts, "rebuild") and opts.rebuild then
                stored = { version: _version(), identity: opts.identity, taken: {} }
            else
                error { message: "intake.open: the ledger at " + path
                                 + " is corrupt (" + st.message + "). Nothing was"
                                 + " read. Pass { rebuild: true } to start a new"
                                 + " one, which will re-import every file the"
                                 + " source still holds.",
                        code: 1004, source: "intake" }
            end if
        end if
        if st.status = "missing" then
            stored = { version: _version(), identity: opts.identity, taken: {} }
        end if
        if st.status = "loaded" then
            stored = st.value
            _check_stored(stored, opts, path)
        end if

        led = { path: path,
                journal: _journal_path(path),
                version: _version(),
                identity: opts.identity,
                ready: opts.ready,
                max_hash: _default_max_hash(),
                taken: stored.taken,
                replayed: 0 }
        if has(opts, "max_hash") then
            led.max_hash = opts.max_hash
        end if
        led = _replay(led)
        return led
    end function

    ' A ledger records what a feed MEANS by "the same file", so a run that
    ' declares a different rule is recording two kinds of fact under one roof --
    ' an idempotency bug that reads as a working program. Refused by naming both.
    function _check_stored(stored, opts, path)
        if type(stored) != "record" or not has(stored, "taken") then
            error { message: "intake.open: " + path + " is not an intake ledger"
                             + " (no taken: record). Nothing was read.",
                    code: 1004, source: "intake" }
        end if
        if has(stored, "version") and stored.version > _version() then
            error { message: "intake.open: the ledger at " + path + " was written"
                             + " by a later version of intake (format "
                             + string(stored.version) + "; this one reads "
                             + string(_version()) + "). Nothing was read.",
                    code: 1004, source: "intake" }
        end if
        if has(stored, "identity") and stored.identity != opts.identity then
            error { message: "intake.open: the ledger at " + path + " records its"
                             + " files by " + string(stored.identity)
                             + " and this run declared " + string(opts.identity)
                             + ". Two identity rules in one ledger cannot both be"
                             + " right, so nothing was read.",
                    code: 1004, source: "intake" }
        end if
        return nothing
    end function

    ' Replay is the STREAMING loop, which is the whole reason a journal is
    ' affordable: a crashed run can leave one far larger than the snapshot, and
    ' reading it a line at a time costs the longest line rather than the file.
    function _replay(led)
        j {file}= led.journal
        if not exists(j) then
            return led
        end if
        n = 0
        for each line in j
            if line != "" then
                e = decode(line)
                led.taken[e.id] = { at: e.at, path: e.path, relative: e.relative,
                                    size: e.size, modified: e.modified,
                                    meta: e.meta }
                n = n + 1
            end if
        end for
        led.replayed = n
        return led
    end function

    ' Fold the journal into the snapshot and release the lock. The journal is
    ' removed only AFTER the snapshot is durable, because the other order loses
    ' every entry the journal held if the write fails -- the same ordering rule
    ' the archive step follows for a file.
    function close(led)
        if led.replayed > 0 then
            _compact(led)
        end if
        lockfile {file}= led.path + ".lock"
        unlock(lockfile)
        return nothing
    end function

    ' PRIVATE: `close` is the only door, because compacting without releasing
    ' the lock leaves a run holding a ledger it has already folded.
    function _compact(led)
        persist.write_atomic(led.path, { version: _version(),
                                         identity: led.identity,
                                         taken: led.taken })
        j {file}= led.journal
        if exists(j) then
            delete(j)
        end if
        return nothing
    end function

    ' ---- readiness ---------------------------------------------------------

    ' Is this file finished arriving? Each rule is a fact about the WRITER, and
    ' the one that asks nothing of the writer is the one that cannot promise.
    function _is_ready(led, entry)
        r = led.ready
        if type(r) = "string" and r = "renamed" then
            return true
        end if
        if type(r) = "string" and r = "sentinel" then
            s {file}= entry.path + ".done"
            return exists(s)
        end if
        ' { stable_for: <duration> } -- a probability. The window has to be
        ' longer than the writer's longest pause and the reader cannot know
        ' that, which is why this is offered and not defaulted to.
        if is_unknown(entry.modified) then
            return false
        end if
        return entry.modified <= now() - r.stable_for
    end function

    ' ---- identity ----------------------------------------------------------

    ' What makes two files the same file. The prefix is part of the key so that
    ' a ledger opened under the wrong rule cannot accidentally MATCH -- it is
    ' refused by _check_stored, and this makes the refusal belt-and-braces.
    function _identity_of(led, entry)
        if led.identity = "path" then
            return "path:" + entry.path
        end if
        if led.identity = "stamp" then
            ' FREE, and that is new information: the listing record already
            ' carries size and modified, added 2026-10-15 for exactly this kind
            ' of filter, so stamp identity costs no call per candidate at all.
            ' The design doc called it "cheap"; it is free.
            stamp = entry.path + ":" + string(entry.size) + ":"
            if is_unknown(entry.modified) then
                stamp = stamp + "?"
            else
                stamp = stamp + string(epoch(entry.modified))
            end if
            return "stamp:" + stamp
        end if
        if entry.size > led.max_hash then
            error { message: "intake: " + entry.path + " is " + string(entry.size)
                             + " bytes and identity \"content\" reads the whole file"
                             + " (just over twice the file in memory, measured)."
                             + " The bound here is " + string(led.max_hash)
                             + " bytes. Use identity \"stamp\", or raise max_hash:"
                             + " if this machine has the memory.",
                    code: 1004, source: "intake" }
        end if
        f {file}= entry.path
        return "sha256:" + hex_encode(sha256(read(f)))
    end function

    ' ---- the scan ----------------------------------------------------------

    ' A root may be a {dir} reference or a plain string, with or without
    ' trailing slashes, so it is normalised through the path builtins rather
    ' than by string surgery: `directory_name(join_path(root, "x"))` answers the
    ' same thing for /data, /data/, /data// and a reference, measured.
    function _normalise_root(root)
        return directory_name(join_path(root, "x"))
    end function

    ' The path UNDER the root, which is what mirrors a source tree into an
    ' archive tree and what distinguishes 2026/Q1/vendor.xlsx from
    ' 2026/Q2/vendor.xlsx -- whose `name` is the same string. DOGFOOD item 6
    ' filed this as a missing field on the listing record; the answer is that it
    ' belongs HERE, because the library knows the root and the platform does not.
    function _relative_to(norm, path)
        if norm = "/" then
            return mid(path, 1, len(path) - 1)
        end if
        if len(path) <= len(norm) + 1 then
            return file_name(path)
        end if
        return mid(path, len(norm) + 1, len(path) - len(norm) - 1)
    end function

    ' Every file matching `pattern` under `root` that this ledger has not
    ' recorded -- and that is finished arriving. The entry is the listing record
    ' with `identity` and `relative` attached: `taken` uses the identity rather
    ' than recomputing it, which under "content" would hash every file twice.
    function arrived(led, root, pattern)
        norm = _normalise_root(root)
        out = []
        for each entry in files(root, pattern)
            e = entry
            e.relative = _relative_to(norm, entry.path)
            if _is_ready(led, e) then
                id = _identity_skipping_vanished(led, e)
                if id != "" then
                    e.identity = id
                    if not has(led.taken, id) then
                        append(out, e)
                    end if
                end if
            end if
        end for
        return out
    end function

    ' A file can vanish between the listing and the read -- another process
    ' archiving it, or a sender withdrawing a drop. Measured, `read` and
    ' `file_size` RAISE with a named reason while `exists` answers a value, so
    ' the check is a question rather than a caught error; the scan reports it
    ' and CONTINUES, because one withdrawn file must not throw away a scan of
    ' ten thousand.
    function _identity_skipping_vanished(led, entry)
        f {file}= entry.path
        if not exists(f) then
            warning("intake: " + entry.path + " vanished during the scan"
                    + " -- skipped, not taken")
            return ""
        end if
        return _identity_of(led, entry)
    end function

    ' ---- recording ---------------------------------------------------------

    ' Record that `entry` was imported, durably, before the next one is read.
    ' One appended line, which is the cheapest durable write there is.
    function taken(led, entry, meta)
        if type(entry) != "record" or not has(entry, "identity") then
            error { message: "intake.taken expects an entry from intake.arrived"
                             + " (it carries the identity that scan computed)."
                             + " Passing a bare path would make this recompute it,"
                             + " which under identity \"content\" means hashing"
                             + " every file twice.",
                    code: 1004, source: "intake" }
        end if
        if not json_encodable(meta) then
            error { message: "intake.taken: meta must be representable as JSON --"
                             + " numbers, text, booleans, nothing, arrays and"
                             + " records. A datetime or a duration is refused;"
                             + " store epoch(when) and seconds instead, which is"
                             + " what this library does with its own times.",
                    code: 1004, source: "intake" }
        end if
        record = { at: epoch(now()),
                   path: entry.path,
                   relative: entry.relative,
                   size: entry.size,
                   modified: _epoch_or_nothing(entry.modified),
                   meta: meta }
        j {file}= led.journal
        append(j, json_encode({ id: entry.identity,
                                at: record.at,
                                path: record.path,
                                relative: record.relative,
                                size: record.size,
                                modified: record.modified,
                                meta: record.meta }) + "\n")
        led.taken[entry.identity] = record
        led.replayed = led.replayed + 1
        return led
    end function

    function _epoch_or_nothing(when)
        if is_unknown(when) then
            return nothing
        end if
        return epoch(when)
    end function

    ' ---- reading the ledger back -------------------------------------------

    ' Has this ledger seen `entry` before? The question `arrived` answers for a
    ' whole directory, for a caller holding one file.
    function seen(led, entry)
        if type(entry) = "record" and has(entry, "identity") then
            return has(led.taken, entry.identity)
        end if
        return has(led.taken, _identity_of(led, entry))
    end function

    function count_taken(led)
        return count(keys(led.taken))
    end function

    ' The recorded entry, with its times as DATETIMES rather than the epochs the
    ' ledger file has to store.
    function entry_for(led, id)
        if not has(led.taken, id) then
            return nothing
        end if
        e = led.taken[id]
        out = { path: e.path, relative: e.relative, size: e.size,
                meta: e.meta, at: from_epoch(e.at) }
        if e.modified = nothing then
            out.modified = unknown
        else
            out.modified = from_epoch(e.modified)
        end if
        return out
    end function

end library
