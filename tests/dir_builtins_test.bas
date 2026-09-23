' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' `exists` on a directory, and `make_dir(path, { parents: true })`.
'
' WHAT THIS REPLACED. `stdlib/persist.bas` carried a 24-line segment walk
' called `ensure_dir`, and the walk was not a matter of taste: `make_dir` had
' no parents mode, and `exists` would not take a directory reference, so each
' level had to be guarded through a FILE reference to a path that is not a
' file. Both halves are fixed and the walk is one call.
'
' WHAT DELIBERATELY DID NOT CHANGE, and it is asserted here rather than in a
' comment: `make_dir` on its own is still NOT idempotent. A bare mkdir is
' ATOMIC, so "did I create it" is how a program takes a lock across processes
' without one, and an existing directory has to stay an error for that to keep
' working -- the argument `accounting` already made for refusing a second close
' rather than making closing idempotent.
'
' SELF-CHECKING rather than golden: every defect here is an ORDINARY-LOOKING
' BOOLEAN. An `exists` that always answers true and one that answers correctly
' are the same transcript on a fixture that only ever asks about things that
' are there.

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

program main(args)

    root = args[0]
    tally = { checks: 0, mismatches: 0 }

    print "-- exists takes a directory reference"
    here {dir}= root
    check("an existing directory", exists(here), true)
    ' THE CONTROL. Without it, "true" is satisfied by an exists that never
    ' looks at anything.
    gone {dir}= root + "/no-such-folder"
    check("an absent directory", exists(gone), false)

    print ""
    print "-- and the file reference is untouched"
    write({file}(root + "/present.txt"), "x")
    check("an existing file", exists({file}(root + "/present.txt")), true)
    check("an absent file", exists({file}(root + "/absent.txt")), false)

    print ""
    print "-- make_dir with parents"
    check("a deep path is created", make_dir(root + "/a/b/c", { parents: true }), true)
    check("and it is a folder", file_type(root + "/a/b/c"), "folder")
    check("every level exists, not just the last", file_type(root + "/a/b"), "folder")
    ' "It returned true" is satisfied by a no-op that returns true, so the
    ' directory has to be USABLE.
    write({file}(root + "/a/b/c/inside.txt"), "written")
    check("the created directory is writable", read({file}(root + "/a/b/c/inside.txt")), "written")
    check("saying it again is not an error", make_dir(root + "/a/b/c", { parents: true }), true)

    print ""
    print "-- THE CONTROL: a bare make_dir is still not idempotent"
    ' This is the half that must NOT have changed. `mkdir` is atomic, so a
    ' program uses "did I create it" as a lock; if an existing directory
    ' stopped being an error, every such program would silently think it held
    ' the lock. Asserted through `on error goto next`, which is the only way a
    ' fixture can state that something raises and keep running.
    on error goto next
    make_dir(root + "/a")
    if error then
        error.clear()
        check("an existing directory is still refused", true, true)
    else
        check("an existing directory is still refused", false, true)
    end if

    make_dir(root + "/a", { parents: false })
    if error then
        error.clear()
        check("parents: false is exactly the plain form", true, true)
    else
        check("parents: false is exactly the plain form", false, true)
    end if

    ' ...and the same call on a name nobody has taken must SUCCEED, or
    ' "it is refused" would be satisfied by a make_dir that refuses everything.
    made = make_dir(root + "/fresh")
    if error then
        error.clear()
        check("CONTROL: a fresh name is still created", false, true)
    else
        check("CONTROL: a fresh name is still created", made, true)
    end if

    print ""
    print "-- refusals, each beside its nearest legal neighbour"
    make_dir(root + "/z", { parent: true })
    if error then
        m = error.message
        error.clear()
        check("an unknown option is refused by name", contains(m, "unknown option 'parent'"), true)
    else
        check("an unknown option is refused by name", false, true)
    end if

    make_dir(root + "/z", { parents: 1 })
    if error then
        m = error.message
        error.clear()
        check("a non-boolean parents is refused", contains(m, "must be true or false"), true)
    else
        check("a non-boolean parents is refused", false, true)
    end if

    make_dir(root + "/present.txt/deeper", { parents: true })
    if error then
        m = error.message
        error.clear()
        check("a component taken by a file is refused, by name",
              contains(m, "exists and is not a directory") and contains(m, "present.txt"), true)
    else
        check("a component taken by a file is refused, by name", false, true)
    end if

    ' THE NARROWING CONTROL. `exists` widened to accept a directory; nothing
    ' else in its group did, because reading or locking a folder is meaningless.
    read(here)
    if error then
        m = error.message
        error.clear()
        check("read still refuses a directory reference", contains(m, "expects a file reference"), true)
    else
        check("read still refuses a directory reference", false, true)
    end if

    lock(here)
    if error then
        error.clear()
        check("lock still refuses a directory reference", true, true)
    else
        unlock(here)
        check("lock still refuses a directory reference", false, true)
    end if

    print ""
    print "checks: " + string(tally.checks)
    print "mismatches: " + string(tally.mismatches)
end program
