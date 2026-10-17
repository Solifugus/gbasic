' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' METADATA: what a file says about itself, and changing it.
'
' `permissions`, `chmod`, `set_mtime`, `is_symlink`, `symlink` -- and the
' correction that came out of measuring around them, which is the sharpest
' thing here: `file_type` and the LISTING disagreed about a dangling symlink.
'
' SELF-CHECKING RATHER THAN GOLDEN, forced twice over. A mode is a PLAUSIBLE
' FOUR DIGITS -- `permissions` reporting the access bits where the mode belongs,
' or the decimal 420 where 0644 belongs, each reads as an answer -- and a golden
' would record it as expected. And `symlink` is the first builtin in this tree
' whose absence a SUITE had to work around: three runners build links with
' `ln -s` in bash, and tests/run_scratch.sh says in as many words that its
' load-bearing tier lives in the runner because gBASIC could not make one.
'
' WHAT IS NOT ASSERTED HERE, and the runner says why rather than this file
' pretending: the mode's VALUE against `stat` (an oracle has to be outside), the
' mode-versus-access difference (which needs root or a read-only mount), and a
' FIFO (which gBASIC cannot create either).

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

    f {file}= join_path(root, "subject.txt")
    write(f, "subject")

    print "-- permissions reports TWO different things"

    p = permissions(f)
    ' THE MODE IS TEXT AND THAT IS FORCED rather than chosen: gBASIC has no
    ' octal literal. Measured -- `0o600` is a parse error (the lexer reads it as
    ' a duration unit) and `0600` is the number six hundred -- so a numeric mode
    ' could not be written correctly even if it were offered.
    check("the mode is text", type(p.mode), "string")
    check("...four octal digits", byte_count(p.mode), 4)
    check("...with a leading zero", starts_with(p.mode, "0"), true)
    ' `readable`/`writable` are what THIS PROCESS can do (access(2)), which is a
    ' different question from what the bits say -- see the runner's tier.
    check("a file we just wrote is readable", p.readable, true)
    check("...and writable", p.writable, true)
    check("...and not executable", p.executable, false)

    print "-- chmod, and the number that would succeed wrongly"

    chmod(f, "0600")
    check("chmod sets the mode", permissions(f).mode, "0600")
    chmod(f, "0644")
    check("and sets it back", permissions(f).mode, "0644")
    chmod(f, "0400")
    check("a read-only file reports not writable", permissions(f).writable, false)
    check("...and still reports readable", permissions(f).readable, true)
    chmod(f, "0644")
    chmod(f, "755")
    check("three digits are a mode too", permissions(f).mode, "0755")
    check("...and now it is executable", permissions(f).executable, true)
    chmod(f, "0644")

    d {dir}= join_path(root, "sub")
    make_dir(d)
    check("a directory has permissions too", byte_count(permissions(d).mode), 4)
    check("...and is searchable", permissions(d).executable, true)

    print "-- set_mtime: the write half of file_mtime"

    when {datetime}= "2021-03-04 05:06:07"
    set_mtime(f, when)
    check("the stamp reads back", string(file_mtime(f)), string(when))
    ' COPY DOES NOT PRESERVE A TIMESTAMP, which is why this exists: an archive
    ' step that keeps the source's own time had no spelling at all.
    c {file}= join_path(root, "copied.txt")
    copy(f, c)
    check("PREMISE: copy did not preserve it", string(file_mtime(c)) != string(when), true)
    set_mtime(c, file_mtime(f))
    check("...and now the copy carries the source's time",
          string(file_mtime(c)), string(when))

    print "-- symlinks: asking, and making one"

    check("a regular file is not a symlink", is_symlink(f), false)
    check("a directory is not a symlink", is_symlink(d), false)

    link {file}= join_path(root, "link.txt")
    check("symlink creates one", symlink(f, link), true)
    check("...and it IS one", is_symlink(link), true)
    ' `file_type` FOLLOWS a link -- measured, which is exactly why `is_symlink`
    ' had to exist: the one call that could have answered reports the TARGET.
    check("file_type follows it", file_type(link), "file")
    check("reading through it gives the target's bytes", read(link), "subject")
    check("the target is not itself a link", is_symlink(f), false)

    ' DELETING A LINK DELETES THE LINK. If `symlink` had made a copy -- or if
    ' `delete` followed links -- this check and the next would disagree.
    delete(link)
    check("delete removes the link", exists(link), false)
    check("...and the target survives", read(f), "subject")

    print "-- the correction: file_type and the LISTING must agree"

    ' MEASURED 2026-10-16: the listing fell back to "file" for anything that was
    ' not a directory, so a DANGLING link, a FIFO, a socket and a device all
    ' arrived labelled `file` while `file_type` said `unknown` or `other`. Two
    ' routes to one question disagreeing, and the listing held the wrong one:
    ' there is no file there. `if e.type = "file" then read(e.path)` raises on
    ' the first and BLOCKS FOREVER on the second.
    dangling {file}= join_path(root, "dangling")
    symlink(join_path(root, "no-such-target"), dangling)
    check("a dangling link IS a symlink", is_symlink(dangling), true)
    check("...and exists() denies it", exists(dangling), false)
    check("file_type says unknown", string(file_type(dangling)), "unknown")

    seen = { dangling: "", subject: "", sub: "" }
    for each e in list(work)
        if e.name = "dangling" then
            seen.dangling = e.type
        end if
        if e.name = "subject.txt" then
            seen.subject = e.type
        end if
        if e.name = "sub" then
            seen.sub = e.type
        end if
    end for
    ' THE AGREEMENT, and it is the load-bearing check of this block.
    check("PREMISE: the listing still REPORTS the dangling entry",
          seen.dangling != "", true)
    check("the listing agrees with file_type about it", seen.dangling, "unknown")
    ' THE TWO CONTROLS, without which "they agree" is satisfied by a listing
    ' that labels everything unknown.
    check("CONTROL: an ordinary file is still file", seen.subject, "file")
    check("CONTROL: a directory is still folder", seen.sub, "folder")

    ' AND `files()` MUST STILL RETURN THEM, which is the check that would have
    ' caught the mistake this correction made on its first attempt. The glob
    ' filtered on `type = "file"` exactly, so relabelling a dangling link
    ' silently DROPPED it from the listing -- and `intake`'s whole withdrawn-
    ' file report rests on the entry still being reported, so the silence that
    ' tier exists to prevent came straight back. `files` excludes FOLDERS now.
    found = { dangling: false, subject: false, sub: false }
    for each e in files(work, "*")
        if e.name = "dangling" then
            found.dangling = true
        end if
        if e.name = "subject.txt" then
            found.subject = true
        end if
        if e.name = "sub" then
            found.sub = true
        end if
    end for
    check("files() still returns a dangling link", found.dangling, true)
    check("CONTROL: ...and an ordinary file", found.subject, true)
    check("CONTROL: ...and still excludes a directory", found.sub, false)

    print "-- refusals, each beside its nearest legal neighbour"

    on error goto next

    r = chmod(f, 0600)
    said = ""
    if error then
        said = error.message
        error.clear()
    end if
    refused("a NUMERIC mode is refused", said != "")
    ' THE MESSAGE CARRIES THE ARITHMETIC, because six hundred is a perfectly
    ' good mode_t and would have set bits nobody asked for AND SUCCEEDED -- so
    ' falling through to the ordinary type check refuses the call while naming
    ' nothing a reader can act on. Captured ABOVE rather than read here: `error`
    ' passed to a helper is CLAIMED by that read, so the first version of this
    ' put the message check inside an `if error then` that was already false,
    ' and it never ran at all.
    check("...and the message says the number is decimal",
          contains(said, "no octal literal"), true)
    check("and the mode was not changed", permissions(f).mode, "0644")

    r = chmod(f, "0999")
    refused("a mode with non-octal digits is refused", error)
    if error then
        error.clear()
    end if

    r = chmod(f, "77777")
    refused("a mode of more than four digits is refused", error)
    if error then
        error.clear()
    end if

    r = chmod(f, "")
    refused("an empty mode is refused", error)
    if error then
        error.clear()
    end if

    missing {file}= join_path(root, "no-such-file")
    r = permissions(missing)
    refused("permissions on a missing path is refused", error)
    if error then
        error.clear()
    end if

    r = set_mtime(f, 1614834367)
    said = ""
    if error then
        said = error.message
        error.clear()
    end if
    refused("a NUMBER is refused as a time", said != "")
    ' A ledger that stores epochs would otherwise pass one here and the call
    ' would WORK, until the day somebody passed milliseconds and the file was
    ' stamped in the year 56000. The remedy has to be in the message.
    check("...and the message names from_epoch", contains(said, "from_epoch"), true)

    r = set_mtime(f, "2021-03-04")
    refused("a string is refused as a time", error)
    if error then
        error.clear()
    end if

    t {time}= "05:06:07"
    r = set_mtime(f, t)
    refused("a TIME-only value is refused: it names no instant", error)
    if error then
        error.clear()
    end if

    taken {file}= join_path(root, "subject.txt")
    r = symlink(f, taken)
    refused("a link whose path is already taken is refused", error)
    if error then
        error.clear()
    end if
    check("and the file that was in the way is untouched", read(f), "subject")

    r = permissions(f, "extra")
    refused("permissions with two arguments is refused", error)
    if error then
        error.clear()
    end if

    r = chmod(f)
    refused("chmod with one argument is refused", error)
    if error then
        error.clear()
    end if

    r = is_symlink()
    refused("is_symlink with no argument is refused", error)
    if error then
        error.clear()
    end if

    ' THE CONTROLS for the refusal block: without them every check above is
    ' satisfied by a family that refuses everything.
    check("CONTROL: chmod still works", chmod(f, "0640"), true)
    check("CONTROL: permissions still answers", permissions(f).mode, "0640")
    check("CONTROL: set_mtime still works", set_mtime(f, when), true)
    ahead {file}= join_path(root, "ahead")
    check("CONTROL: a link to something that does not exist YET is allowed",
          symlink(join_path(root, "arrives-later"), ahead), true)
    check("...which is how a link is written before its target arrives",
          is_symlink(ahead), true)

    print "checks: " + string(tally.checks)
    print "mismatches: " + string(tally.mismatches)
end program
