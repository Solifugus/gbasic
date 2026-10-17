' SCRATCH SPACE AND METADATA, on every platform — and the Windows measurements.
'
' This file has two jobs, and the output distinguishes them.
'
'   `ok` lines assert what must be IDENTICAL on both platforms. A `MISMATCH`
'   names both sides and fails the suite.
'
'   `REPORT` lines print what is EXPECTED TO DIVERGE. They assert nothing, and
'   they are the measurement: `docs/windows_port_status.md` §27 lists what each
'   one is claimed to be, and a Windows run either confirms the claim or turns
'   it into a defect. PASTE THEM BACK.
'
' Everything here was written on Linux and the Windows half of it has NEVER
' EXECUTED — the cross-compile ratchet proves `src/platform_win32.c` compiles
' against real `windows.h` and nothing more. §5 of that page is the standing
' reason this file exists at all: a socket is not a file descriptor on Windows,
' so code can compile cleanly and fail at runtime, and no error count notices.
'
' Run it from the repository root, like the rest of tests/windows:
'     gbasic tests/windows/scratch_metadata.bas

function ok(label, got, want)
    tally.checks = tally.checks + 1
    if string(got) = string(want) then
        print "ok   " + label
    else
        tally.mismatches = tally.mismatches + 1
        print "MISMATCH " + label + ": got " + string(got) + ", want " + string(want)
    end if
    return nothing
end function

function report(label, value)
    print "REPORT " + label + ": " + string(value)
    return nothing
end function

program main(args)

    tally = { checks: 0, mismatches: 0, no_symlinks: false }

    ' THE FIXTURE USES THE FEATURE TO GET ITS OWN WORKSPACE, which it could not
    ' do before today: this file runs from the repository root and must leave
    ' nothing behind, and there is no shell here to make a scratch directory.
    work = temp_dir({ prefix: "gbwin-" })
    ok("temp_dir answers a directory reference", type(work), "directory")
    ok("...which exists", exists(work), true)
    report("the system temporary directory", directory_name(work))

    print "-- a temp file is created, not named --"

    t = temp_file({ in: work, prefix: "stage-" })
    ok("temp_file exists the moment it is answered", exists(t), true)
    ok("...and is empty", file_size(t), 0)
    t2 = temp_file({ in: work })
    ok("two calls give two names", string(t) != string(t2), true)
    write(t, "payload")
    ok("it is writable", read(t), "payload")

    ' THE INCREMENT MUST AGREE WITH ITSELF ABOUT SEPARATORS, which is a claim
    ' independent of which separator the platform uses -- and it FAILED on
    ' Windows (§28): the temp body joined with a backslash while the four path
    ' splitters scanned for '/' only, so `file_name` answered the WHOLE PATH
    ' and `directory_name` of a file made `in:` a directory answered that
    ' directory's PARENT. Two fixes landed for it: the temp body now joins with
    ' '/' (the rule `gb_realpath` and `gb_exe_path` already follow), and the
    ' splitters read `path_is_sep`, which accepts both on Windows.
    named = temp_file({ in: work, prefix: "vendor-" })
    ok("a prefix reaches the NAME, not the whole path",
       starts_with(file_name(named), "vendor-"), true)
    ok("...and the name is not the whole path",
       byte_count(file_name(named)) < byte_count(string(named)), true)
    ok("directory_name of a file made `in:` d IS d",
       directory_name(named), string(work))
    report("a path this build produced", string(named))
    ' THE PLATFORM HALF, reported rather than asserted: on Windows a backslash
    ' IS a separator and on POSIX it is an ordinary filename character, so this
    ' line is expected to differ and `tests/path_split_test.bas` asserts the
    ' POSIX side.
    report("file_name(\"C:\\data\\x.csv\")", file_name("C:\\data\\x.csv"))

    ' THE MEASUREMENT THAT DECIDES WHETHER ACL WORK IS NEEDED AT ALL. POSIX
    ' gets 0600 from mkstemp, which is what lets a program hold a secret in one.
    ' Windows cannot promise a mode and inherits the directory's ACL -- but the
    ' system temp directory is normally PER USER, so the file may be private by
    ' inheritance, in which case the honest fix is a sentence of documentation
    ' rather than a DACL. Check the printed path with: icacls <path>
    report("temp file mode", permissions(t).mode)
    report("temp dir mode", permissions(work).mode)
    report("temp file path (run icacls on this)", string(t))

    print "-- the safe-write pattern, which is what in: is for --"

    ' `atomic_replace` is same-filesystem only, so a temp file must be able to
    ' land BESIDE its destination. This is the shape every careful write takes.
    dest {file}= join_path(string(work), "final.txt")
    staged = temp_file({ in: work, prefix: "swap-" })
    write(staged, "swapped in")
    ok("atomic_replace accepts a temp file made with in:",
       atomic_replace(staged, dest), true)
    ok("...and the destination holds the new bytes", read(dest), "swapped in")

    print "-- permissions, chmod, set_mtime --"

    f {file}= join_path(string(work), "subject.txt")
    write(f, "subject")
    p = permissions(f)
    ok("the mode is text", type(p.mode), "string")
    ok("...four octal digits", byte_count(p.mode), 4)
    ok("a file we just wrote is readable", p.readable, true)
    ' UNKNOWN, NEVER FALSE, on a platform with no execute bit: Windows decides
    ' executability by extension, so `false` would be a claim nothing supports.
    report("executable (unknown on Windows, a boolean on POSIX)", p.executable)

    ' CHMOD IS WHERE THE PLATFORMS PART. POSIX honours every bit; Windows has
    ' ONE of the twelve -- the read-only attribute, from the owner write bit --
    ' and WARNS about the rest (warning 2114) rather than refusing, so a program
    ' hardening a file still gets the bit Windows has. The warning is
    ' UNREACHABLE on POSIX, so Windows is the only place it can be observed:
    ' if it does not appear on stderr there, the `dropped` out-parameter is
    ' wrong rather than the warning.
    chmod(f, "0600")
    report("mode after chmod(f, \"0600\")", permissions(f).mode)
    chmod(f, "0400")
    report("mode after chmod(f, \"0400\")", permissions(f).mode)
    report("writable after chmod 0400", permissions(f).writable)
    chmod(f, "0644")

    ' A NUMBER IS REFUSED BY NAME on both platforms: gBASIC has no octal
    ' literal, so `0600` is the decimal six hundred -- a perfectly good mode_t
    ' that would set bits nobody asked for AND SUCCEED.
    on error goto next
    r = chmod(f, 0600)
    if error then
        ok("a numeric mode is refused", true, true)
        error.clear()
    else
        ok("a numeric mode is refused", false, true)
    end if

    when {datetime}= "2021-03-04 05:06:07"
    ok("set_mtime stamps a file", set_mtime(f, when), true)
    ' IDENTICAL ON BOTH PLATFORMS AND ASSERTED, not reported: SetFileTime works
    ' in UTC FILETIME ticks, which is why gb_file_mtime does not use the C
    ' runtime's st_mtime -- that field is computed through two timezone
    ' conversions and shifts the epoch whenever TZ disagrees with the system.
    ok("...and it reads back", string(file_mtime(f)), string(when))

    print "-- symlinks --"

    ok("a regular file is not a symlink", is_symlink(f), false)
    link {file}= join_path(string(work), "link.txt")
    made = symlink(f, link)
    if error then
        ' WINDOWS NEEDS SeCreateSymbolicLinkPrivilege: Developer Mode, or an
        ' elevated process. The message must NAME it -- a bare "permission
        ' denied" sends an author to look at the filesystem, where there is
        ' nothing to find, because this is a POLICY decision and not a file
        ' permission. Reported rather than asserted, since whether the
        ' privilege is held is a fact about the machine.
        report("symlink refused", error.message)
        tally.no_symlinks = true
        error.clear()
    else
        ok("symlink creates one", made, true)
        ok("...and it IS one", is_symlink(link), true)
        ok("reading through it gives the target's bytes", read(link), "subject")
        delete(link)
        ok("deleting the link leaves the target", read(f), "subject")
    end if

    print "-- remove_tree, where two Windows defects are predicted --"

    ' PREDICTION 1: a READ-ONLY FILE. Windows `unlink` fails with EACCES on
    ' FILE_ATTRIBUTE_READONLY, so a tree holding one should stop part-way --
    ' the diagnostic correct and the behaviour not, since rm -rf's Windows
    ' equivalents clear the attribute first. On POSIX the directory's write
    ' permission is what matters and a read-only FILE is removed happily.
    tree {dir}= join_path(string(work), "tree")
    make_dir(tree)
    ro {file}= join_path(string(tree), "readonly.txt")
    write(ro, "locked")
    chmod(ro, "0400")
    removed = remove_tree(tree)
    if error then
        report("remove_tree over a read-only file REFUSED", error.message)
        error.clear()
    else
        report("remove_tree over a read-only file", string(removed))
    end if
    report("the tree is gone", string(not exists(tree)))

    ' PREDICTION 2: a DIRECTORY SYMLINK inside the tree, which on Windows is a
    ' REPARSE POINT. `gb_is_symlink` answers 1 for any reparse point -- that is
    ' the glob's cycle guard -- and the walk then calls `unlink`, which cannot
    ' remove a directory reparse point; RemoveDirectoryW is needed. FAILING IS
    ' THE GOOD OUTCOME HERE. The bad one is descending into it, so what is
    ' ASSERTED on both platforms is that the target survives.
    outside {dir}= join_path(string(work), "precious")
    make_dir(outside)
    keep {file}= join_path(string(outside), "keep.txt")
    write(keep, "do not lose me")
    tree2 {dir}= join_path(string(work), "tree2")
    make_dir(tree2)
    inner {file}= join_path(string(tree2), "plain.txt")
    write(inner, "ordinary")
    dirlink {dir}= join_path(string(tree2), "dir_link")
    linked = symlink(outside, dirlink)
    if error then
        report("could not create the directory link, so this case was NOT measured", error.message)
        error.clear()
    else
        r2 = remove_tree(tree2)
        if error then
            report("remove_tree over a directory link REFUSED", error.message)
            error.clear()
        else
            report("remove_tree over a directory link", string(r2))
        end if
        ' THE ASSERTION. Content, not presence: a walk that descended the link
        ' and emptied the target would leave the directory there.
        ok("the link's target and its content survived", read(keep), "do not lose me")
    end if

    print "-- the listing agrees with file_type --"

    ' Measured on Linux 2026-10-16: the listing fell back to "file" for anything
    ' that was not a directory, so a DANGLING link arrived labelled `file` while
    ' `file_type` said `unknown`. Windows reaches the same code, so the
    ' agreement is asserted rather than reported.
    dang {file}= join_path(string(work), "dangling")
    dr = symlink(join_path(string(work), "no-such-target"), dang)
    if error then
        report("could not create the dangling link, so the agreement was NOT measured",
               error.message)
        error.clear()
    else
        ok("a dangling link is a symlink", is_symlink(dang), true)
        ok("...and exists() denies it", exists(dang), false)
        ok("file_type says unknown", string(file_type(dang)), "unknown")
        seen = ""
        for each e in list(work)
            if e.name = "dangling" then
                seen = e.type
            end if
        end for
        ok("the listing agrees", seen, "unknown")
    end if

    ' CLEAN UP AFTER ITSELF, which this suite's whole family requires: nothing
    ' is removed automatically, and a gate that leaves temp directories behind
    ' accumulates them.
    ' THE PROVOCATION ABOVE MUST NOT COST A CLEAN EXIT. Prediction 1 leaves a
    ' read-only file behind on Windows, so the first version of this left a
    ' `gbwin-*` directory in %TEMP% on every run (§28) -- a fixture that
    ' accumulates state is the shape run_repl.sh had to fix after greeting
    ' somebody with "5721 previous sessions ended without saving". The
    ' attribute is cleared and the removal retried, which is also the FIX
    ' prediction 1 asks of the platform layer, done here by hand.
    if exists(ro) then
        chmod(ro, "0644")
        if error then
            error.clear()
        end if
    end if
    cleaned = remove_tree(work)
    if error then
        report("the fixture could not remove its own workspace", error.message)
        error.clear()
        ' SECOND ATTEMPT, after clearing every read-only file the walk met.
        for each e in files(work, "**/*")
            chmod(e.path, "0644")
            if error then
                error.clear()
            end if
        end for
        cleaned = remove_tree(work)
        if error then
            report("...and could not after clearing read-only attributes either",
                   error.message)
            error.clear()
        else
            report("...but could once the read-only attributes were cleared", "true")
        end if
    end if

    ' THE FLOOR KNOWS WHY IT IS LOW. Without the symlink privilege five checks
    ' legitimately cannot run (§28: 15 of 24 on Windows with Developer Mode
    ' off), so a fixed floor would either fail a correct run or pass a broken
    ' one. `tally.no_symlinks` is set where the privilege is refused.
    '
    ' AND `BROKEN` IS THE LAST LINE, because run_windows_suite.sh passes a file
    ' whose last line is `mismatches: 0` -- so the first version printed BROKEN
    ' and then the tally, and the runner reported PASS (15) over a file that had
    ' just called itself broken. Measured on Windows.
    floor = 24
    if tally.no_symlinks then
        floor = 14
    end if
    print "checks: " + string(tally.checks)
    if tally.checks < floor then
        print("BROKEN: only " + string(tally.checks) + " checks ran, wanted "
              + string(floor))
    else
        print "mismatches: " + string(tally.mismatches)
    end if
end program
