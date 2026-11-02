' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' `intake.repeats` and `intake.set_aside` (docs/intake_design.md §6a;
' gbasic-books volume 3, gap G4): a file sent again is reported rather than
' left in the drop folder for good. Self-checking, for the reason
' intake_test.bas gives: a repeats that answers nothing looks exactly like a
' quiet day.
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

function names(entries)
    out = []
    for each e in entries
        append(out, e.relative)
    end for
    return out
end function

function refusal(f, a, b)
    on error goto next
    x = f(a, b)
    e = error
    if e then
        return e.message
    end if
    return "accepted"
end function

program main(args)
    root = args[0]
    tally = { checks: 0, mismatches: 0 }
    drop = root + "/drop"
    make_dir(drop, { parents: true })
    a {file}= drop + "/a.csv"
    write(a, "sku,price\nA-1,5\n")

    print "-- content identity"
    led = intake.open({file}(root + "/ledger.json"), { identity: "content", ready: "renamed" })
    first = intake.arrived(led, {dir}drop, "*.csv")
    for each f in first
        led = intake.taken(led, f, {})
    end for
    check("the first file was taken", intake.count_taken(led), 1)
    check("a taken file still in the drop folder is not a repeat of itself", names(intake.repeats(led, {dir}drop, "*.csv")), [])

    b {file}= drop + "/b.csv"
    write(b, "sku,price\nA-1,5\n")
    c {file}= drop + "/c.csv"
    write(c, "sku,price\nA-1,6\n")
    check("a copy under another name does not arrive", names(intake.arrived(led, {dir}drop, "*.csv")), ["c.csv"])
    r = intake.repeats(led, {dir}drop, "*.csv")
    check("it is a repeat", names(r), ["b.csv"])
    check("naming the file it repeats", r[0].repeats.path, drop + "/a.csv")
    check("with when that was taken, as a datetime", type(r[0].repeats.at), "datetime")
    check("and the identity they share", r[0].identity, first[0].identity)

    led = intake.archive(led, first[0], { to: {dir}(root + "/archive"), keep: 30 days })
    write(a, "sku,price\nA-1,5\n")
    r = intake.repeats(led, {dir}drop, "*.csv")
    check("the same content dropped again at an archived file's path is a repeat", names(r), ["a.csv", "b.csv"])
    check("and says where the first one went", r[0].repeats.archived_to, root + "/archive/a.csv")

    print "-- set_aside"
    q = root + "/resent"
    went = intake.set_aside(r[1], { to: {dir}q })
    check("set_aside answers where it went", went, q + "/b.csv")
    moved {file}= q + "/b.csv"
    check("the repeat is there", exists(moved), true)
    check("and gone from the drop folder", exists(b), false)
    check("the ledger is not changed by it", intake.count_taken(led), 1)
    check("so the repeat no longer appears", names(intake.repeats(led, {dir}drop, "*.csv")), ["a.csv"])
    write(b, "sku,price\nA-1,5\n")
    r = intake.repeats(led, {dir}drop, "*.csv")
    check("a second resend of b refuses to overwrite the first",
          contains(refusal(intake.set_aside, r[1], { to: {dir}q }), "already exists"), true)
    check("and leaves it in the drop folder", exists(b), true)
    went = intake.set_aside(r[1], { to: {dir}q, on_collision: "replace" })
    check("replace says so and moves it", exists(b), false)
    check("the archived original is untouched by any of it", intake.entry_for(led, first[0].identity).archived_to, root + "/archive/a.csv")

    print "-- refusals"
    fresh = intake.arrived(led, {dir}drop, "*.csv")
    check("set_aside refuses an entry that is not a repeat",
          contains(refusal(intake.set_aside, fresh[0], { to: {dir}q }), "expects an entry from intake.repeats"), true)
    check("set_aside requires to:",
          contains(refusal(intake.set_aside, r[0], {}), "the directory the repeat is moved into"), true)
    check("set_aside refuses an unknown option",
          contains(refusal(intake.set_aside, r[0], { to: {dir}q, keep: 1 day }), "unknown option 'keep'"), true)
    check("set_aside refuses a misspelt collision rule",
          contains(refusal(intake.set_aside, r[0], { to: {dir}q, on_collision: "overwrite" }), "on_collision must be"), true)

    print "-- path identity"
    pdrop = root + "/pdrop"
    make_dir(pdrop, { parents: true })
    p {file}= pdrop + "/p.csv"
    write(p, "one\n")
    pled = intake.open({file}(root + "/pledger.json"), { identity: "path", ready: "renamed" })
    pf = intake.arrived(pled, {dir}pdrop, "*.csv")
    pled = intake.taken(pled, pf[0], {})
    write(p, "one, changed\n")
    check("under path, the taken file still there is the original", names(intake.repeats(pled, {dir}pdrop, "*.csv")), [])
    pled = intake.archive(pled, pf[0], { to: {dir}(root + "/parchive"), keep: 30 days })
    write(p, "two\n")
    check("and a file dropped again at its path after the archive is a repeat", names(intake.repeats(pled, {dir}pdrop, "*.csv")), ["p.csv"])

    intake.close(led)
    intake.close(pled)
    print ""
    print "checks: " + string(tally.checks)
    print "mismatches: " + string(tally.mismatches)
end program
