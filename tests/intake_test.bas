' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' `intake` increment 1 -- open / arrived / taken / close.
'
' SELF-CHECKING RATHER THAN GOLDEN, and here it is forced harder than usual:
' every defect this library can have produces AN ORDINARY-LOOKING SUCCESSFUL
' RUN. A ledger that silently records nothing, an identity that always matches,
' a readiness rule that always says yes -- each prints the same transcript as a
' working import, and a golden would record one as expected and defend it. The
' whole point of the library is that it answers "have I had this already", and
' both wrong answers look like success.
'
' What is NOT here, because it needs the filesystem or a second process, and
' lives in tests/run_intake.sh instead: the three-way IDENTITY difference (which
' needs two files with identical size and mtime), the stable_for window (which
' needs a backdated file), a file that vanishes mid-scan (a dangling symlink),
' the CRASH tier (which needs a real SIGKILL), and the concurrency tier.

load intake

function check(label, got, want)
    tally.checks = tally.checks + 1
    if string(got) = string(want) then
        print "ok   " + label
    else
        tally.mismatches = tally.mismatches + 1
        print "MISMATCH " + label + ": got " + string(got) + ", want " + string(want)
    end if
    return nothing
end function

' A refusal, stated the only way a fixture can state one: armed, called,
' claimed. `error.clear()` in the same frame, because PLAT-ERR rule 1 makes a
' second raise while one is pending escape the frame.
function refused(label, raised, message)
    tally.checks = tally.checks + 1
    if raised then
        print "ok   " + label
    else
        tally.mismatches = tally.mismatches + 1
        print "MISMATCH " + label + ": it was ACCEPTED (" + message + ")"
    end if
    return nothing
end function

program main(args)

    root = args[0]
    tally = { checks: 0, mismatches: 0 }

    data = root + "/data"
    make_dir(data, { parents: true })
    make_dir(data + "/2026/Q1", { parents: true })
    make_dir(data + "/2026/Q2", { parents: true })
    ' TWO FILES WITH THE SAME NAME in different folders, which is the shape
    ' DOGFOOD item 6 was filed about: `name` is `vendor.csv` for both.
    a {file}= data + "/2026/Q1/vendor.csv"
    b {file}= data + "/2026/Q2/vendor.csv"
    write(a, "q1\n")
    write(b, "q2-longer\n")

    print "-- a first run takes what is there"
    led = intake.open({file}(root + "/ledger.json"),
                      { identity: "content", ready: "renamed" })
    check("a new ledger is empty", intake.count_taken(led), 0)
    first = intake.arrived(led, {dir}data, "**/*.csv")
    check("both files arrived", count(first), 2)
    ' SORTED BY PATH, which the glob guarantees, so Q1 comes before Q2 and the
    ' checks below can name them.
    check("the relative path is what distinguishes them",
          first[0].relative + "|" + first[1].relative,
          "2026/Q1/vendor.csv|2026/Q2/vendor.csv")
    check("and their NAMES do not", first[0].name, first[1].name)
    check("the entry carries the identity the scan computed",
          left(first[0].identity, 7), "sha256:")
    check("and the listing fields are still there",
          string(first[0].size) + "/" + first[0].type, "3/file")

    for each f in first
        led = intake.taken(led, f, { rows: 7 })
    end for
    check("both are recorded", intake.count_taken(led), 2)

    print "-- durability is per file, before the run ends"
    ' THE JOURNAL IS THE CRASH STORY and this is the half a fixture can see:
    ' the line is on disk BEFORE close, which is what makes the run survivable.
    ' That the survivor is READ BACK correctly is the runner's CRASH tier.
    j {file}= root + "/ledger.json.journal"
    check("the journal exists before close", exists(j), true)
    check("it holds one line per file taken", count(read_lines(j)), 2)
    check("the snapshot is not written yet", exists({file}(root + "/ledger.json")), false)

    print "-- asking the ledger about one file"
    check("seen() agrees with the scan", intake.seen(led, first[0]), true)
    e = intake.entry_for(led, first[0].identity)
    check("the recorded entry keeps the relative path", e.relative, "2026/Q1/vendor.csv")
    check("its meta is the caller's", e.meta.rows, 7)
    ' THE LEDGER STORES EPOCHS because strict JSON refuses a datetime, and the
    ' library converts back at its edge -- so a caller never sees the number.
    check("and its times come back as datetimes", type(e.at), "datetime")
    check("including the file's own modified time", type(e.modified), "datetime")

    intake.close(led)
    check("close writes the snapshot", exists({file}(root + "/ledger.json")), true)
    check("and removes the journal it folded in", exists(j), false)

    print "-- a second run takes nothing, which is the whole point"
    led2 = intake.open({file}(root + "/ledger.json"),
                       { identity: "content", ready: "renamed" })
    check("the ledger came back", intake.count_taken(led2), 2)
    again = intake.arrived(led2, {dir}data, "**/*.csv")
    check("nothing arrived", count(again), 0)
    ' AN EMPTY RUN IS A NORMAL TUESDAY: an empty array, never an error.
    check("and that is an empty array, not a failure", type(again), "array")

    print "-- a new file arrives beside taken ones"
    c {file}= data + "/2026/Q2/extra.csv"
    write(c, "new\n")
    third = intake.arrived(led2, {dir}data, "**/*.csv")
    check("only the new one arrived", count(third), 1)
    check("and it is the right one", third[0].name, "extra.csv")
    led2 = intake.taken(led2, third[0], {})
    check("an empty meta is fine", intake.count_taken(led2), 3)
    intake.close(led2)

    print "-- the ledger may live in the directory it scans"
    ' The glob requires a leading dot to be matched explicitly, so a dotted
    ' ledger beside the data cannot feed itself to itself -- which is exactly
    ' what the design sketch's /data/.intake.json assumed without saying so.
    led3 = intake.open({file}(data + "/.intake.json"),
                       { identity: "path", ready: "renamed" })
    hidden = intake.arrived(led3, {dir}data, "**/*")
    names = []
    for each h in hidden
        append(names, h.name)
    next
    check("a `**/*` scan does not see the dotted ledger",
          contains(names, ".intake.json"), false)
    check("while it does see the data", count(hidden), 3)
    intake.close(led3)

    print "-- readiness: sentinel"
    sdir = root + "/sentinel"
    make_dir(sdir, { parents: true })
    s1 {file}= sdir + "/part.csv"
    write(s1, "half a file\n")
    sled = intake.open({file}(root + "/sentinel.json"),
                       { identity: "path", ready: "sentinel" })
    check("a file with no .done is NOT ready",
          count(intake.arrived(sled, {dir}sdir, "*.csv")), 0)
    done {file}= sdir + "/part.csv.done"
    write(done, "")
    ready = intake.arrived(sled, {dir}sdir, "*.csv")
    ' THE CONTROL. Without it "0 arrived" is satisfied by a readiness rule that
    ' never says yes, which is a library that imports nothing for ever.
    check("once the sentinel appears it IS ready", count(ready), 1)
    check("and the sentinel itself is not an arrival", ready[0].name, "part.csv")
    intake.close(sled)

    print "-- the refusals, each beside the call that must still work"
    on error goto next

    r = intake.open({file}(root + "/r1.json"), { ready: "renamed" })
    refused("no identity: is refused", error, "")
    if error then
        m1 = error.message
        error.clear()
        check("and the message names all three choices",
              contains(m1, "\"path\"") and contains(m1, "\"stamp\"")
              and contains(m1, "\"content\""), true)
    end if

    r = intake.open({file}(root + "/r2.json"), { identity: "content" })
    refused("no ready: is refused", error, "")
    if error then
        m2 = error.message
        error.clear()
        check("and that message names its three too",
              contains(m2, "renamed") and contains(m2, "sentinel")
              and contains(m2, "stable_for"), true)
    end if

    r = intake.open({file}(root + "/r3.json"), { identity: "sha", ready: "renamed" })
    refused("an unknown identity is refused", error, "")
    if error then
        error.clear()
    end if

    r = intake.open({file}(root + "/r4.json"), { identity: "path", ready: "soon" })
    refused("an unknown ready is refused", error, "")
    if error then
        error.clear()
    end if

    r = intake.open({file}(root + "/r5.json"),
                    { identity: "path", ready: { stable_for: 30 } })
    refused("stable_for must be a duration, not a number", error, "")
    if error then
        error.clear()
    end if

    r = intake.open({file}(root + "/r6.json"),
                    { identity: "path", ready: "renamed", identtiy: "path" })
    refused("an unknown option field is refused BY NAME", error, "")
    if error then
        m6 = error.message
        error.clear()
        check("and the message quotes the typo", contains(m6, "identtiy"), true)
    end if

    ' The ledger written above records by CONTENT. Re-opening it as "path"
    ' would record two kinds of fact under one roof.
    r = intake.open({file}(root + "/ledger.json"),
                    { identity: "path", ready: "renamed" })
    refused("a ledger may not change its identity rule", error, "")
    if error then
        m7 = error.message
        error.clear()
        check("and the message names both rules",
              contains(m7, "content") and contains(m7, "path"), true)
    end if

    bad {file}= root + "/corrupt.json"
    write(bad, "{not json at all")
    r = intake.open(bad, { identity: "path", ready: "renamed" })
    refused("a corrupt ledger is refused rather than rebuilt", error, "")
    if error then
        m8 = error.message
        error.clear()
        check("and it says what would make it start anyway",
              contains(m8, "rebuild"), true)
    end if

    later {file}= root + "/later.json"
    write(later, "{\"version\":99,\"identity\":\"path\",\"taken\":{}}")
    r = intake.open(later, { identity: "path", ready: "renamed" })
    refused("a ledger from a LATER intake is refused", error, "")
    if error then
        error.clear()
    end if

    notaledger {file}= root + "/other.json"
    write(notaledger, "{\"something\":\"else\"}")
    r = intake.open(notaledger, { identity: "path", ready: "renamed" })
    refused("a well-formed JSON file that is not a ledger is refused", error, "")
    if error then
        error.clear()
    end if

    tled = intake.open({file}(root + "/t.json"),
                       { identity: "path", ready: "renamed" })
    r = intake.taken(tled, { path: data + "/2026/Q1/vendor.csv" }, {})
    refused("taken() refuses an entry arrived() did not yield", error, "")
    if error then
        error.clear()
    end if

    tarr = intake.arrived(tled, {dir}data, "*.csv")
    r = intake.taken(tled, { path: "x", identity: "path:x", relative: "x",
                             size: 1, modified: unknown }, { when: now() })
    refused("taken() refuses a meta a ledger cannot store", error, "")
    if error then
        m9 = error.message
        error.clear()
        check("and names the remedy", contains(m9, "epoch"), true)
    end if

    ' THE CONTROLS FOR THE WHOLE REFUSAL BLOCK. Without these, every check
    ' above is satisfied by an `open` that refuses everything -- which for this
    ' library means importing nothing, silently, for ever.
    ok1 = intake.open({file}(root + "/c1.json"),
                      { identity: "stamp", ready: "renamed" })
    check("CONTROL: a well-formed open is accepted", type(ok1), "record")
    intake.close(ok1)
    ok2 = intake.open({file}(root + "/c2.json"),
                      { identity: "path", ready: { stable_for: 30 seconds },
                        max_hash: 1024 })
    check("CONTROL: a stable_for window and max_hash are accepted",
          type(ok2.ready), "record")
    intake.close(ok2)
    ok3 = intake.open(bad, { identity: "path", ready: "renamed", rebuild: true })
    check("CONTROL: a corrupt ledger starts empty when the caller says so",
          intake.count_taken(ok3), 0)
    intake.close(ok3)
    ok4 = intake.open({file}(root + "/ledger.json"),
                      { identity: "content", ready: "renamed" })
    check("CONTROL: re-opening with the SAME identity works",
          intake.count_taken(ok4), 3)
    intake.close(ok4)
    intake.close(tled)

    print "-- archive: the tree is mirrored and the original moves"
    adir = root + "/archive"
    aled = intake.open({file}(root + "/arch.json"),
                       { identity: "content", ready: "renamed" })
    acand = intake.arrived(aled, {dir}data, "**/vendor.csv")
    check("two files to archive", count(acand), 2)
    for each f in acand
        aled = intake.taken(aled, f, { rows: 1 })
        aled = intake.archive(aled, f, { to: {dir}adir, keep: 180 days })
    end for
    ' THE RELATIVE PATH IS WHAT THE MIRROR NEEDS: both files are `vendor.csv`,
    ' so a mirror built from `name` would put one on top of the other.
    check("Q1 landed under its own folder",
          exists({file}(adir + "/2026/Q1/vendor.csv")), true)
    check("and Q2 under its own",
          exists({file}(adir + "/2026/Q2/vendor.csv")), true)
    check("the original is gone", exists({file}(data + "/2026/Q1/vendor.csv")), false)
    ' `copy` does not create parents and `archive` does -- without that the
    ' mirror is impossible and nothing else here would work.
    check("the mirrored folder was created", file_type(adir + "/2026/Q2"), "folder")
    arow = intake.entry_for(aled, acand[0].identity)
    check("the entry records where it went", arow.archived_to,
          adir + "/2026/Q1/vendor.csv")
    check("and when, as a datetime", type(arow.archived), "datetime")
    check("and the retention, as a duration", type(arow.keep), "duration")
    ' 180 days is 15552000 seconds EXACTLY. The first implementation computed
    ' it as `epoch(from_epoch(0) + d)`, which is local-time arithmetic across a
    ' DST boundary and answered 15548400 -- an hour short, and a plausible
    ' number nobody would question.
    check("retention is exact, with no calendar in it",
          arow.keep.total_seconds, 15552000)

    print "-- purge reports by default and deletes only when told"
    ' Nothing is due for 180 days, so the bare call must find nothing.
    r1 = intake.purge(aled)
    check("the default run is a dry run", r1.dry_run, true)
    check("nothing is due yet", r1.due, 0)
    check("and it considered both entries", r1.considered, 2)
    ' `as_of` states a moment rather than sleeping through one.
    r2 = intake.purge(aled, { as_of: now() + 200 days })
    check("past the retention they are due", r2.due, 2)
    check("the report names the bytes", r2.bytes, 13)
    check("and lists the items", count(r2.items), 2)
    check("each item carries where the file went",
          contains(r2.items[0].archived_to, "/2026/Q"), true)
    check("and when it was archived, as a datetime",
          type(r2.items[0].archived), "datetime")
    ' THE LOAD-BEARING HALF: a dry run DELETED NOTHING. A report is only worth
    ' having if the files are still there after it.
    check("a dry run purged nothing", r2.purged, 0)
    check("and the archived file is still there",
          exists({file}(adir + "/2026/Q1/vendor.csv")), true)

    r3 = intake.purge(aled, { as_of: now() + 200 days, dry_run: false })
    check("the real run purged them", r3.purged, 2)
    check("the file is gone", exists({file}(adir + "/2026/Q1/vendor.csv")), false)
    ' RULE 2: THE ENTRY OUTLIVES THE FILE. Removed with it, the file would be
    ' NEWLY ARRIVED the next time it is re-dropped -- which under "path"
    ' identity means importing it again once a cycle, for ever.
    aled = r3.ledger
    check("the entry survives the file", intake.count_taken(aled), 2)
    prow = intake.entry_for(aled, acand[0].identity)
    check("marked purged", type(prow.purged), "datetime")
    check("and its meta is dropped -- the one field a caller sizes",
          count(keys(prow.meta)), 0)
    ' And a second purge finds nothing: a purged entry is not a candidate.
    r4 = intake.purge(aled, { as_of: now() + 400 days })
    check("a purged entry is not due again", r4.due, 0)
    intake.close(aled)

    print "-- purge may not touch what was never archived"
    ' RULE 1, and it is asserted as a DIFFERENCE: an entry taken and never
    ' archived is not a candidate HOWEVER OLD, while the archived one beside it
    ' is. Either half alone passes on a purge that always answers one way.
    ndir = root + "/never"
    make_dir(ndir, { parents: true })
    n1 {file}= ndir + "/kept.csv"
    n2 {file}= ndir + "/moved.csv"
    write(n1, "never archived
")
    write(n2, "archived
")
    nled = intake.open({file}(root + "/never.json"),
                       { identity: "path", ready: "renamed" })
    for each f in intake.arrived(nled, {dir}ndir, "*.csv")
        nled = intake.taken(nled, f, {})
        if f.name = "moved.csv" then
            nled = intake.archive(nled, f, { to: {dir}(root + "/never_arch"),
                                             keep: 1 second })
        end if
    end for
    rn = intake.purge(nled, { as_of: now() + 10 years, dry_run: false })
    nled = rn.ledger
    check("only the archived one was purged", rn.purged, 1)
    check("CONTROL: the file that was never archived is untouched",
          exists(n1), true)
    intake.close(nled)

    print "-- the archive refusals, each beside the call that must still work"
    on error goto next

    xdir = root + "/x"
    make_dir(xdir, { parents: true })
    x1 {file}= xdir + "/one.csv"
    write(x1, "one
")
    xled = intake.open({file}(root + "/x.json"),
                       { identity: "path", ready: "renamed" })
    xcand = intake.arrived(xled, {dir}xdir, "*.csv")

    r = intake.archive(xled, xcand[0], { to: {dir}(root + "/xa"), keep: 1 day })
    refused("archive before taken is refused", error, "")
    if error then
        mx = error.message
        error.clear()
        check("and the message names the order", contains(mx, "intake.taken"), true)
    end if

    xled = intake.taken(xled, xcand[0], {})
    r = intake.archive(xled, xcand[0], { to: {dir}(root + "/xa") })
    refused("keep: is required", error, "")
    if error then
        error.clear()
    end if

    r = intake.archive(xled, xcand[0], { keep: 1 day })
    refused("to: is required", error, "")
    if error then
        error.clear()
    end if

    r = intake.archive(xled, xcand[0], { to: {dir}(root + "/xa"), keep: 30 })
    refused("keep: must be a duration, not a bare number", error, "")
    if error then
        error.clear()
    end if

    ' MONTHS AND YEARS HAVE NO FIXED LENGTH, so they cannot be stored as
    ' seconds without inventing one. Refused by name, with the remedy.
    r = intake.archive(xled, xcand[0], { to: {dir}(root + "/xa"), keep: 6 months })
    refused("keep: 6 months is refused -- no fixed length", error, "")
    if error then
        mm = error.message
        error.clear()
        check("and it says to use days", contains(mm, "180 days"), true)
    end if
    ' A REFUSED ARCHIVE LEAVES THE FILE WHERE IT WAS, and this check is here
    ' because the months refusal originally ran AFTER the move: the file was
    ' archived and then the call raised, leaving it in the archive with no
    ' ledger record of it -- so the next run could find neither. Nothing
    ' asserted it until the refusal order was fixed.
    check("a refused archive moved nothing", exists(x1), true)
    check("and created nothing in the archive",
          is_unknown(file_type(root + "/xa/one.csv")), true)

    r = intake.archive(xled, xcand[0], { to: {dir}(root + "/xa"), keep: 1 day,
                                         keepp: 2 })
    refused("an unknown archive option is refused by name", error, "")
    if error then
        error.clear()
    end if

    ' THE COLLISION. `copy` overwrites in silence, and under identity "content"
    ' a corrected re-drop has the SAME relative path as the file it corrects --
    ' so a second archive would destroy the first with nothing said.
    xled = intake.archive(xled, xcand[0], { to: {dir}(root + "/xa"), keep: 1 day })
    write(x1, "one again
")
    x2led = intake.open({file}(root + "/x2.json"),
                        { identity: "content", ready: "renamed" })
    x2cand = intake.arrived(x2led, {dir}xdir, "*.csv")
    x2led = intake.taken(x2led, x2cand[0], {})
    r = intake.archive(x2led, x2cand[0], { to: {dir}(root + "/xa"), keep: 1 day })
    refused("an existing archive target is refused", error, "")
    if error then
        mc = error.message
        error.clear()
        check("and the message offers on_collision", contains(mc, "on_collision"), true)
    end if
    check("CONTROL: the first archived file is still intact",
          read({file}(root + "/xa/one.csv")), "one
")

    r = intake.archive(x2led, x2cand[0], { to: {dir}(root + "/xa"), keep: 1 day,
                                           on_collision: "whatever" })
    refused("an unknown on_collision value is refused", error, "")
    if error then
        error.clear()
    end if

    ' AND THE CONTROL FOR THE COLLISION RULE: declared, it replaces.
    x2led = intake.archive(x2led, x2cand[0], { to: {dir}(root + "/xa"),
                                               keep: 1 day,
                                               on_collision: "replace" })
    check("CONTROL: on_collision replace does what it says",
          read({file}(root + "/xa/one.csv")), "one again
")

    ' Archiving twice would move a file that is no longer there.
    r = intake.archive(x2led, x2cand[0], { to: {dir}(root + "/xa"), keep: 1 day,
                                           on_collision: "replace" })
    refused("archiving the same entry twice is refused", error, "")
    if error then
        error.clear()
    end if

    r = intake.purge(xled, { dry_run: "yes" })
    refused("purge dry_run must be a boolean", error, "")
    if error then
        error.clear()
    end if

    r = intake.purge(xled, { when: now() })
    refused("an unknown purge option is refused by name", error, "")
    if error then
        error.clear()
    end if

    pc = intake.purge(xled)
    check("CONTROL: a bare purge still reports", pc.dry_run, true)
    intake.close(xled)
    intake.close(x2led)

    print "checks: " + string(tally.checks)
    print "mismatches: " + string(tally.mismatches)
end program
