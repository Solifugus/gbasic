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

    print "checks: " + string(tally.checks)
    print "mismatches: " + string(tally.mismatches)
end program
