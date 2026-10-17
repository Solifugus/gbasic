' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' SCRATCH SPACE: `temp_file`, `temp_dir` and `remove_tree`.
'
' The three shipped together because they are one job: make somewhere to work,
' and get rid of it afterwards. Before them a program had neither end -- there
' was no way to ask for a unique name, so `persist.bas` writes `path + ".tmp"`
' beside its destination and two writers of one file share one temp name; and
' `remove_dir` removes only an EMPTY directory, so nothing in the language
' could remove a tree it had just built.
'
' SELF-CHECKING RATHER THAN GOLDEN, and here that is forced: every defect in
' this family produces AN ORDINARY-LOOKING SUCCESS. A `temp_file` that answers
' a name without creating the file looks identical until two programs collide;
' a `remove_tree` that FOLLOWS a symlink reports the same `true` while removing
' somebody else's data; a prefix that escapes its directory puts the file
' somewhere the caller did not name and says nothing. A golden would record
' every one of those as expected and defend it.
'
' THE TWO CLAIMS THIS FIXTURE CANNOT MAKE are in the runner, because gBASIC
' cannot create a symlink and cannot read a file's mode: whether a symlink
' inside a tree is unlinked rather than descended, and whether a temp file
' arrives at 0600. Both are the load-bearing half of their own claim.

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

function refused(label, raised)
    tally.checks = tally.checks + 1
    if raised then
        print "ok   " + label
    else
        tally.mismatches = tally.mismatches + 1
        print "MISMATCH " + label + ": it was ACCEPTED"
    end if
    return nothing
end function

program main(args)

    root = args[0]
    tally = { checks: 0, mismatches: 0 }
    work {dir}= root

    print "-- a temporary file is CREATED, not merely named"

    ' THE LOAD-BEARING CHECK OF THE WHOLE FAMILY. A function that hands back an
    ' unused name hands back a RACE: two programs asking at the same moment are
    ' told the same name, and the second overwrites the first. That is why
    ' `mkstemp` exists rather than a "pick me a name" call, and it is the one
    ' thing a caller cannot see from the value they are given.
    t = temp_file()
    check("it exists the moment it is answered", exists(t), true)
    check("it is empty", file_size(t), 0)
    check("it is a file reference, so write/read/delete compose", type(t), "file")

    write(t, "payload")
    check("it is writable", read(t), "payload")

    ' A NAME GENERATOR THAT REPEATS IS A NAME GENERATOR WITH NO PURPOSE, and
    ' the defect is invisible in a single-process test that only ever asks once.
    t2 = temp_file()
    check("two calls give two names", string(t) != string(t2), true)
    check("and the second one also exists", exists(t2), true)

    p = temp_file({ prefix: "vendor-" })
    check("a prefix reaches the name", starts_with(file_name(p), "vendor-"), true)
    check("and the prefix does not become the whole name",
          byte_count(file_name(p)) > byte_count("vendor-"), true)

    print "-- `in:` is what makes a SAFE WRITE writable"

    ' MEASURED, NOT ASSUMED, AND THE REASON THE OPTION EXISTS: `atomic_replace`
    ' is same-filesystem only and says so by name, and the system temporary
    ' directory is routinely on another filesystem -- /tmp is tmpfs on this
    ' machine while the repository is on ext4. So a temp file in the system
    ' directory CANNOT be swapped onto a destination beside the program's own
    ' data, which is the one thing the safe-write pattern is for.
    '
    ' The CONTROL for this pair -- that the system-directory temp file really
    ' is refused across devices -- is in the runner, because whether two paths
    ' sit on one filesystem is a fact about the machine and a tier that cannot
    ' establish its own premise must say so rather than pass.
    staged = temp_file({ in: work, prefix: "stage-" })
    check("it lands in the directory asked for", directory_name(staged), root)
    write(staged, "swapped in")
    dest {file}= join_path(root, "final.txt")
    check("atomic_replace accepts it", atomic_replace(staged, dest), true)
    check("and the destination holds the new bytes", read(dest), "swapped in")
    check("the temp file is gone, having BECOME the destination",
          exists(staged), false)

    ' NOTHING IS REMOVED AUTOMATICALLY, and that is the design rather than an
    ' omission: in the pattern just above the temp file is MEANT to outlive the
    ' call that made it, since it becomes the destination. An interpreter that
    ' swept temp files at exit would break exactly that.
    survivor = temp_file({ in: work })
    check("a temp file nobody moved is still there", exists(survivor), true)

    print "-- a temporary directory, and getting rid of it"

    d = temp_dir({ in: work, prefix: "scratch-" })
    check("it is a directory reference", type(d), "directory")
    check("it exists", exists(d), true)
    check("it is empty", count(list(d)), 0)
    check("it lands where asked", directory_name(d), root)

    d2 = temp_dir({ in: work })
    check("two calls give two directories", string(d) != string(d2), true)

    ' COMPOSITION IS WHY THE TWO SHIPPED TOGETHER: `temp_dir` makes the scratch
    ' space and `remove_tree` is the only thing in the language that can remove
    ' it once anything has been written into it.
    inner {file}= join_path(string(d), "note.txt")
    write(inner, "working")
    deeper {dir}= join_path(string(d), "sub")
    make_dir(deeper)
    buried {file}= join_path(string(deeper), "buried.txt")
    write(buried, "down here")
    check("PREMISE: the tree has content", exists(buried), true)
    check("remove_tree removes it", remove_tree(d), true)
    check("the tree is gone", exists(d), false)
    check("and so is what was inside it", exists(buried), false)

    ' THE CONTROL that separates "removes a tree" from "removes anything that
    ' happens to be a directory with things in it": an EMPTY directory is still
    ' removed, so `remove_tree` is not a function that needs content to work.
    check("an empty directory is removed too", remove_tree(d2), true)

    print "-- refusals, each beside its nearest legal neighbour"

    on error goto next

    r = temp_file({ prefix: "a/b" })
    refused("a prefix containing a separator is refused", error)
    if error then
        error.clear()
    end if
    ' THE HALF THAT MATTERS: the refusal must be a refusal, not a file created
    ' somewhere else first. `prefix: "../x"` is the shape that would put the
    ' file outside the directory the caller named.
    escaped {file}= join_path(directory_name(root), "escaped-probe")
    r = temp_file({ in: work, prefix: "../escaped-probe" })
    refused("a prefix that would climb out is refused", error)
    if error then
        error.clear()
    end if
    check("and nothing was created outside the directory", exists(escaped), false)

    r = temp_file({ prefix: "a" + chr(0) + "b" })
    refused("a prefix with an interior NUL is refused", error)
    if error then
        error.clear()
    end if

    r = temp_file({ prefix: 7 })
    refused("a prefix that is not text is refused", error)
    if error then
        error.clear()
    end if

    r = temp_file({ prefx: "typo-" })
    refused("an unknown option is refused BY NAME", error)
    if error then
        error.clear()
    end if

    r = temp_file("vendor-")
    refused("a bare string is refused: the options are named", error)
    if error then
        error.clear()
    end if

    missing {dir}= join_path(root, "no-such-directory")
    r = temp_file({ in: missing })
    refused("a directory that does not exist is refused", error)
    if error then
        error.clear()
    end if

    r = temp_dir({ prefx: "typo-" })
    refused("temp_dir refuses an unknown option too", error)
    if error then
        error.clear()
    end if

    keeper {file}= join_path(root, "keeper.txt")
    write(keeper, "do not remove me")
    r = remove_tree(keeper)
    refused("remove_tree refuses a FILE", error)
    if error then
        error.clear()
    end if
    check("and the file is still there", read(keeper), "do not remove me")

    gone {dir}= join_path(root, "never-existed")
    r = remove_tree(gone)
    refused("remove_tree refuses a missing path", error)
    if error then
        error.clear()
    end if

    ' THE CONTROLS for the whole refusal block: without them every check above
    ' is satisfied by a family that refuses everything.
    ok_file = temp_file({ in: work, prefix: "fine-" })
    check("CONTROL: an ordinary temp_file still works", exists(ok_file), true)
    ok_dir = temp_dir({ in: work })
    check("CONTROL: an ordinary temp_dir still works", exists(ok_dir), true)
    check("CONTROL: an ordinary remove_tree still works", remove_tree(ok_dir), true)

    ' THE FIXTURE CLEANS UP AFTER ITSELF, which is not housekeeping but the
    ' rule this family states: nothing is removed automatically, so a program
    ' that made a temp file in the SYSTEM directory -- the four above that took
    ' no `in:` -- is the only thing that knows when it is done with it. A gate
    ' that leaves them accumulates them, which is what `run_repl.sh` had to fix
    ' after greeting somebody with "5721 previous sessions ended without saving".
    delete(t)
    delete(t2)
    delete(p)
    check("CONTROL: a deleted temp file is really gone", exists(t), false)

    print "checks: " + string(tally.checks)
    print "mismatches: " + string(tally.mismatches)
end program
