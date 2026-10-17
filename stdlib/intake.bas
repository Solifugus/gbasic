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
                led.taken[e.id] = _entry_from(e)
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
            ' REPLAYED FIRST, SO THE JOURNAL IS THE SOURCE OF TRUTH and this
            ' in-memory copy is only an accelerator. Without it, a caller who
            ' dropped a returned ledger -- which is the easiest mistake this
            ' API offers, and one I made writing its own worked example --
            ' would have `close` write a snapshot from the stale value and then
            ' DELETE the journal holding the facts it is missing. Replaying
            ' makes that mistake merely wasteful instead of destructive: the
            ' last line for an id wins, so anything journaled and not in hand
            ' is folded in rather than lost.
            led = _replay(led)
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
                   meta: meta,
                   archived_at: nothing,
                   archived_to: nothing,
                   keep_seconds: nothing,
                   purged_at: nothing }
        _journal(led, entry.identity, record)
        led.taken[entry.identity] = record
        led.replayed = led.replayed + 1
        return led
    end function

    ' ONE READER FOR A LEDGER ROW, shared by the journal replay and the snapshot,
    ' so the two cannot disagree about what a row holds -- the defect this tree
    ' has recorded for `sort` against `>` and for `unique` against `contains`.
    '
    ' THE ARCHIVE FIELDS ARE OPTIONAL ON READ, which is what lets a ledger
    ' written before `archive` existed open unchanged and read as "nothing
    ' archived yet" rather than needing a version bump. `nothing` and absent are
    ' deliberately the same answer here: strict JSON writes a missing value as
    ' null and reads it back as `nothing`, so distinguishing them would be a
    ' distinction the file cannot carry.
    function _entry_from(e)
        row = { at: e.at, path: e.path, relative: e.relative,
                size: e.size, modified: e.modified, meta: e.meta,
                archived_at: nothing, archived_to: nothing,
                keep_seconds: nothing, purged_at: nothing }
        if has(e, "archived_at") then
            row.archived_at = e.archived_at
        end if
        if has(e, "archived_to") then
            row.archived_to = e.archived_to
        end if
        if has(e, "keep_seconds") then
            row.keep_seconds = e.keep_seconds
        end if
        if has(e, "purged_at") then
            row.purged_at = e.purged_at
        end if
        return row
    end function

    ' ONE WRITER, for the same reason. Every state change -- taken, archived,
    ' purged -- is one appended line carrying the whole row, so a replay needs
    ' no notion of a delta and the last line for an id wins.
    function _journal(led, id, row)
        j {file}= led.journal
        append(j, json_encode(merge({ id: id }, row)) + "\n")
        return nothing
    end function

    function _epoch_or_nothing(when)
        if is_unknown(when) then
            return nothing
        end if
        return epoch(when)
    end function

    ' ---- archiving ---------------------------------------------------------

    function _archive_options()
        return ["to", "keep", "on_collision"]
    end function

    function _check_archive_options(opts)
        if type(opts) != "record" then
            error { message: "intake.archive expects an options record, for"
                             + " example { to: {dir}\"/data/archive\","
                             + " keep: 180 days }",
                    code: 1004, source: "intake" }
        end if
        for each k in keys(opts)
            if not contains(_archive_options(), k) then
                error { message: "intake.archive: unknown option '" + k + "'."
                                 + " Known options: "
                                 + join(_archive_options(), ", "),
                        code: 1004, source: "intake" }
            end if
        next
        if not has(opts, "to") then
            error { message: "intake.archive: to: is required -- the directory"
                             + " the file is moved into. Its tree is mirrored"
                             + " from the directory that was scanned, so"
                             + " 2026/Q1/vendor.xlsx lands under 2026/Q1.",
                    code: 1004, source: "intake" }
        end if
        if not has(opts, "keep") then
            error { message: "intake.archive: keep: is required -- how long the"
                             + " archived file is kept, as a duration, for"
                             + " example 180 days. `purge` reads it from the"
                             + " entry, so retention travels with the file and"
                             + " two feeds into one archive may differ.",
                    code: 1004, source: "intake" }
        end if
        if type(opts.keep) != "duration" then
            error { message: "intake.archive: keep: must be a duration, for"
                             + " example 180 days or 12 hours, not "
                             + type(opts.keep) + ". A bare number has no unit,"
                             + " which is the mistake a duration literal makes"
                             + " impossible.",
                    code: 1004, source: "intake" }
        end if
        ' Wrapped in parentheses because a line break continues a statement
        ' only INSIDE brackets (PLAT-CONT) -- there is no trailing marker.
        if (has(opts, "on_collision") and opts.on_collision != "replace"
            and opts.on_collision != "refuse") then
            error { message: "intake.archive: on_collision must be \"refuse\""
                             + " (the default) or \"replace\", not "
                             + string(opts.on_collision),
                    code: 1004, source: "intake" }
        end if
        return nothing
    end function

    ' Move the file into the archive, mirroring the tree it came from, and
    ' record where it went and how long it is kept.
    '
    ' THE ORIGINAL MUST NOT BE LOST, which is the whole job, and the two cases
    ' are told apart BEFORE acting rather than hoped about:
    '
    '   same filesystem -- `atomic_replace` is one rename. The file is either
    '   at the old path or the new one and can never be at neither, nothing is
    '   copied, and no verification is possible or needed.
    '
    '   across filesystems -- it refuses, naming "same filesystem", and leaves
    '   the source in place. Then copy, CHECK THE BYTE COUNT, and only then
    '   delete, so a copy that reported success and wrote short leaves the
    '   original where it was.
    '
    ' `move` is deliberately not used for the second case although it crosses
    ' devices: its fallback is copy-then-unlink with NO CHECK BETWEEN, so a
    ' filesystem that lies at `fclose` loses the file and the ledger records
    ' nothing -- the next run then finds neither the file nor a record of it.
    '
    ' MATCHING ON A MESSAGE IS BRITTLE AND THE BRITTLENESS IS SAFE: if that
    ' sentence is ever reworded every archive takes the copy path and becomes
    ' SLOWER, never less safe. A tier asserts the fast path is real as a
    ' difference, because an optimisation nothing measures can quietly stop.
    '
    ' NOTHING HERE fsyncs, and this does not claim durability -- the reference
    ' says the same of `atomic_replace`. What is promised is narrower: the
    ' archive exists with the source's byte count before the original goes.
    function archive(led, entry, opts)
        _check_archive_options(opts)
        if type(entry) != "record" or not has(entry, "identity") then
            error { message: "intake.archive expects an entry from"
                             + " intake.arrived.",
                    code: 1004, source: "intake" }
        end if
        if not has(led.taken, entry.identity) then
            error { message: "intake.archive: " + entry.path + " has not been"
                             + " taken yet. Call intake.taken first: purge will"
                             + " not delete anything the ledger does not record"
                             + " as archived, and an archive with no entry"
                             + " behind it has nothing to record it in.",
                    code: 1004, source: "intake" }
        end if
        ' THE RETENTION IS RESOLVED BEFORE ANYTHING MOVES, and that ordering is
        ' a correction found by the fixture rather than by reading: months and
        ' years are refused inside `_seconds_in`, which was called at the END,
        ' so `keep: 6 months` MOVED THE FILE and then raised -- leaving the file
        ' in the archive, the ledger with no record of it, and the next run
        ' unable to find either. A refusal after an irreversible act is not a
        ' refusal.
        keep_seconds = _seconds_in(opts.keep)

        row = led.taken[entry.identity]
        if row.archived_at != nothing then
            error { message: "intake.archive: " + entry.path + " was already"
                             + " archived to " + string(row.archived_to)
                             + ". Archiving twice would move a file that is no"
                             + " longer there and overwrite the record of where"
                             + " it went.",
                    code: 1004, source: "intake" }
        end if

        root = directory_name(join_path(opts.to, "x"))
        target = join_path(root, entry.relative)
        collide = "refuse"
        if has(opts, "on_collision") then
            collide = opts.on_collision
        end if
        ' `copy` OVERWRITES AN EXISTING TARGET IN SILENCE (measured), and under
        ' identity "content" a corrected re-drop has the same relative path as
        ' the file it corrects -- so the second archive would destroy the first
        ' with nothing said. Refused by naming both paths; `make_dir` refuses an
        ' existing directory for the same reason rather than being quietly
        ' idempotent.
        existing {file}= target
        if exists(existing) and collide = "refuse" then
            error { message: "intake.archive: " + target + " already exists, so"
                             + " archiving " + entry.path + " would overwrite"
                             + " it. Pass { on_collision: \"replace\" } to say"
                             + " that is what you want.",
                    code: 1004, source: "intake" }
        end if

        source {file}= entry.path
        if not exists(source) then
            error { message: "intake.archive: " + entry.path + " is no longer"
                             + " there. Nothing was archived and the ledger is"
                             + " unchanged.",
                    code: 1004, source: "intake" }
        end if
        before = file_size(source)
        make_dir(directory_name(target), { parents: true })

        on error goto next
        atomic_replace(entry.path, target)
        if error then
            why = error.message
            error.clear()
            if not contains(why, "same filesystem") then
                error { message: "intake.archive: could not move " + entry.path
                                 + " to " + target + " (" + why + ")."
                                 + " Nothing was archived.",
                        code: 1004, source: "intake" }
            end if
            ' ACROSS FILESYSTEMS. Copy, check, then delete -- in that order, so
            ' a short copy leaves the original where it was.
            copy(source, target)
            landed {file}= target
            after = file_size(landed)
            if after != before then
                error { message: "intake.archive: " + target + " is "
                                 + string(after) + " bytes where "
                                 + entry.path + " is " + string(before)
                                 + ". The original has NOT been removed.",
                        code: 1004, source: "intake" }
            end if
            delete(source)
        end if

        row.archived_at = epoch(now())
        row.archived_to = target
        row.keep_seconds = keep_seconds
        _journal(led, entry.identity, row)
        led.taken[entry.identity] = row
        led.replayed = led.replayed + 1
        return led
    end function

    ' A duration in seconds, which is what the ledger file can hold. `keep` is
    ' authored as `180 days` and stored as 15552000 -- the unit is checked at
    ' the door and then there is no unit left to misread.
    '
    ' THE FIRST VERSION OF THIS WAS `epoch(from_epoch(0) + d)` AND IT WAS WRONG
    ' BY AN HOUR: `from_epoch(0)` is a datetime in LOCAL time, and adding 180
    ' days to it crosses a DST boundary, so wall-clock arithmetic answered
    ' 15548400 -- 3600 short, which is a plausible number and the kind that is
    ' never noticed. `total_seconds` asks the duration itself and has no
    ' calendar in it.
    '
    ' AND IT RAISES FOR MONTHS AND YEARS, which is right rather than
    ' inconvenient: those have no fixed length, so `keep: 6 months` cannot be
    ' stored as a number of seconds without inventing one. Re-raised in this
    ' library's own words, naming the remedy, because the bare message is about
    ' durations in general and the reader is holding a retention policy.
    function _seconds_in(d)
        on error goto next
        total = d.total_seconds
        if error then
            error.clear()
            error { message: "intake.archive: keep: " + string(d) + " has no"
                             + " fixed length, because months and years vary."
                             + " Say it in days, hours, minutes or seconds --"
                             + " 180 days rather than 6 months.",
                    code: 1004, source: "intake" }
        end if
        return total
    end function

    ' ---- purging -----------------------------------------------------------

    function _purge_options()
        return ["dry_run", "as_of"]
    end function

    ' What would go, or what went. REPORTS BY DEFAULT: deleting data is the one
    ' thing here that cannot be undone, so `dry_run` is true unless the caller
    ' says otherwise, and the two runs return the SAME SHAPE so they can be
    ' compared rather than read differently.
    ' `opts` DEFAULTS TO `nothing`, so `intake.purge(ledger)` is the reporting
    ' call -- which is the safe default made visible rather than merely
    ' documented. A record default is a parse error by design (PLAT-OPTPARAM
    ' admits literals only, deliberately: gBASIC has no closures, so an
    ' arbitrary default expression would need a scope to be evaluated in), and
    ' `nothing` is the one literal that reads as "none given".
    function purge(led, opts = nothing)
        if opts = nothing then
            opts = { dry_run: true }
        end if
        if type(opts) != "record" then
            error { message: "intake.purge expects an options record, for"
                             + " example { dry_run: false }",
                    code: 1004, source: "intake" }
        end if
        for each k in keys(opts)
            if not contains(_purge_options(), k) then
                error { message: "intake.purge: unknown option '" + k + "'."
                                 + " Known options: "
                                 + join(_purge_options(), ", "),
                        code: 1004, source: "intake" }
            end if
        next
        dry = true
        if has(opts, "dry_run") then
            if type(opts.dry_run) != "boolean" then
                error { message: "intake.purge: dry_run must be true or false,"
                                 + " not " + type(opts.dry_run),
                        code: 1004, source: "intake" }
            end if
            dry = opts.dry_run
        end if
        ' `as_of` exists so a test can state a moment rather than sleep through
        ' one; a caller may use it to ask what a purge would do next month.
        as_of = now()
        if has(opts, "as_of") then
            if type(opts.as_of) != "datetime" then
                error { message: "intake.purge: as_of must be a datetime, not "
                                 + type(opts.as_of),
                        code: 1004, source: "intake" }
            end if
            as_of = opts.as_of
        end if
        cutoff = epoch(as_of)

        ' THE REPORT CARRIES THE LEDGER, and this is a correction found by
        ' running it: the first version returned the report alone, so a real
        ' purge deleted the files, journaled the change, and handed the caller
        ' nothing to rebind -- `close` then wrote a snapshot from the stale
        ' value it still held. A gBASIC record is a VALUE; `taken` and
        ' `archive` return the ledger for exactly this reason and `purge`
        ' cannot return two things, so it returns ONE record holding both.
        ' Naming the field `ledger` is what makes the rebinding visible:
        '
        '     r = intake.purge(led, { dry_run: false })
        '     led = r.ledger
        '
        ' On a dry run `r.ledger` is the same ledger, unchanged, so the two
        ' calls have one shape and one usage.
        report = { dry_run: dry, considered: 0, due: 0, purged: 0,
                   bytes: 0, items: [], ledger: led }
        for each id in keys(led.taken)
            row = led.taken[id]
            report.considered = report.considered + 1
            ' RULE 1: nothing goes that the ledger does not record as ARCHIVED.
            ' An entry taken and never archived is not a candidate however old
            ' it is, and neither is one already purged.
            if row.archived_at != nothing and row.purged_at = nothing then
                if row.archived_at + row.keep_seconds <= cutoff then
                    report.due = report.due + 1
                    report.bytes = report.bytes + row.size
                    append(report.items, { identity: id,
                                           archived_to: row.archived_to,
                                           relative: row.relative,
                                           size: row.size,
                                           archived: from_epoch(row.archived_at) })
                    if not dry then
                        f {file}= row.archived_to
                        if exists(f) then
                            delete(f)
                        end if
                        ' RULE 2: THE ENTRY OUTLIVES THE FILE. An entry removed
                        ' with its file makes that file NEWLY ARRIVED the next
                        ' time it is re-dropped, which under "path" identity
                        ' means importing it again once a cycle, for ever. So
                        ' the row keeps its identity and its archived path and
                        ' drops `meta`, the one field whose size a caller
                        ' controls.
                        row.purged_at = epoch(now())
                        row.meta = {}
                        _journal(led, id, row)
                        led.taken[id] = row
                        report.purged = report.purged + 1
                    end if
                end if
            end if
        next
        report.ledger = led
        return report
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
                meta: e.meta, at: from_epoch(e.at),
                archived_to: e.archived_to,
                archived: unknown, keep: unknown, purged: unknown }
        if e.modified = nothing then
            out.modified = unknown
        else
            out.modified = from_epoch(e.modified)
        end if
        ' The ledger file holds epochs and a count of seconds, because strict
        ' JSON refuses a datetime and a duration; the conversion back belongs
        ' here, at the edge, so a caller never sees the numbers.
        if e.archived_at != nothing then
            out.archived = from_epoch(e.archived_at)
        end if
        if e.purged_at != nothing then
            out.purged = from_epoch(e.purged_at)
        end if
        if e.keep_seconds != nothing then
            ' THE ONLY ROUTE FROM A NUMBER OF SECONDS BACK TO A DURATION, and
            ' it is multiplication: `n seconds` does not parse with a variable
            ' `n` (a duration literal takes a literal), and there is no
            ' `{duration}` modifier beside `{date}`, `{datetime}` and `{time}`.
            ' Measured, and filed. `(1 second) * 15552000` renders `180 days`.
            out.keep = (1 second) * e.keep_seconds
        end if
        return out
    end function

end library
