' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' `stem` -- a filename with its extension taken off, and the INVARIANT that
' keeps it and `extension` from disagreeing about where the boundary is.
'
' WHAT WAS ACTUALLY MISSING. `docs/etl_files_design.md` §1 listed path
' splitting -- "basename/dirname/stem" -- as absent. Measured, two thirds of it
' had been there for months: `file_name` is basename, `directory_name` is
' dirname, and `extension` reports the suffix. Only `stem` did not exist. That
' is the SECOND entry in that list to turn out already-shipped under another
' name, `modified`/`file_mtime` being the first, and both were found by running
' the tree rather than by reading it.
'
' SELF-CHECKING rather than golden, and here it is forced: every defect `stem`
' can have returns A PERFECTLY ORDINARY FILENAME. `stem("..")` answering "."
' and `stem("a.")` answering "a" are both strings a golden would record as
' expected and defend -- and they are exactly what the first implementation
' answered, so this is not a hypothetical.
'
' THE LOAD-BEARING TIER IS THE INVARIANT, not the pinned values. `stem` and
' `extension` are two halves of ONE cut, so what must hold is
'
'     extension empty  ->  stem IS file_name          (nothing to strip)
'     otherwise        ->  stem + "." + extension = file_name
'
' and that is what found the real defect: `extension` answers "" for TWO
' different reasons -- no dot at all, and a dot with nothing after it -- and
' only the first means "nothing to strip". Sharing one scan between the two is
' necessary and NOT sufficient; without the trailing-dot rule the two still
' disagreed while reading the same dot. Two routes to one question disagreeing
' is the shape this tree has recorded for `sort` against `>` on exact integers
' and for `unique` against `contains`.
'
' The invariant carries its own NON-VACUITY FLOOR, because it is satisfied by a
' battery that only asks one of its two questions: the battery must contain
' names with an extension, names without, and at least one name with SEVERAL
' dots -- which is the case that separates "strip the last suffix" from "strip
' everything after the first dot", the commonest wrong implementation.

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

' How many dots the last component carries -- used only by the non-vacuity
' floor, so the battery cannot quietly stop exercising the multi-dot case.
function dots_in_name(p)
    return count(split(file_name(p), ".")) - 1
end function

program main(args)

    tally = { checks: 0, mismatches: 0 }

    ' A battery of the shapes a real intake directory and a hostile caller both
    ' produce. Order is irrelevant; every one of them goes through the same
    ' three questions below.
    battery = [
        "/data/intake/vendor_2026-03.csv",
        "vendor_2026-03.csv",
        "/data/archive.tar.gz",
        "/data/intake/",
        "/data/intake//",
        "/",
        ".",
        "..",
        "...",
        "/home/u/.bashrc",
        ".hidden.txt",
        "/data/noext",
        "noext",
        "",
        "a.",
        "/data/a..b",
        "/data/.",
        "report.2026.Q1.xlsx" ]

    print "-- the invariant: stem and extension are two halves of one cut"
    with_ext = 0
    without_ext = 0
    multi_dot = 0
    broken = 0
    for each p in battery
        name = file_name(p)
        ext = extension(p)
        st = stem(p)
        if ext = "" then
            without_ext = without_ext + 1
            if st != name then
                broken = broken + 1
                print "  BROKEN [" + p + "] no extension but stem " + quote(st) + " is not file_name " + quote(name)
            end if
        else
            with_ext = with_ext + 1
            if st + "." + ext != name then
                broken = broken + 1
                print "  BROKEN [" + p + "] stem " + quote(st) + " + " + quote(ext) + " is not file_name " + quote(name)
            end if
        end if
        if dots_in_name(p) > 1 then
            multi_dot = multi_dot + 1
        end if
        ' Dumped so the ORACLE tier can recompute these from the SAME battery
        ' in an independent implementation. The battery is written once, here;
        ' a second copy in the runner is a second thing that can stop asking
        ' the awkward questions.
        print "BATTERY|" + p + "|" + name + "|" + st + "|" + ext
    end for
    check("every path in the battery satisfies the invariant", broken, 0)

    ' THE FLOOR. Each of these is a question the invariant would otherwise stop
    ' asking in silence -- and the multi-dot count is the one that matters,
    ' since a battery of single-dot names cannot tell the last suffix from the
    ' first.
    check("NON-VACUITY: the battery has names WITH an extension", with_ext >= 5, true)
    check("NON-VACUITY: the battery has names WITHOUT one", without_ext >= 5, true)
    check("NON-VACUITY: the battery has names with SEVERAL dots", multi_dot >= 3, true)

    print "-- the cases where a plausible implementation differs"
    check("the ordinary case", stem("/data/intake/vendor_2026-03.csv"), "vendor_2026-03")
    check("no directory is the same cut", stem("vendor_2026-03.csv"), "vendor_2026-03")
    ' THE LAST suffix, not the first: `extension` already says "gz" rather than
    ' "tar.gz", so a stem of "archive" would make the two disagree.
    check("several dots keep all but the last", stem("/data/archive.tar.gz"), "archive.tar")
    check("three parts keep two", stem("report.2026.Q1.xlsx"), "report.2026.Q1")
    ' A LEADING dot is not an extension, which `extension` has always said, so
    ' a dotfile's stem is the whole name.
    check("a dotfile has nothing to strip", stem("/home/u/.bashrc"), ".bashrc")
    check("a dotfile WITH a suffix strips only the suffix", stem(".hidden.txt"), ".hidden")
    ' A TRAILING dot is not an extension either, and this is the pair the first
    ' implementation got wrong -- both answers were ordinary filenames.
    check("a trailing dot is not a boundary", stem("a."), "a.")
    check("the parent directory is not a dotfile", stem(".."), "..")
    check("the current directory", stem("."), ".")
    check("no dot at all", stem("/data/noext"), "noext")
    check("a trailing slash is tolerated, as file_name does", stem("/data/intake/"), "intake")
    check("a doubled trailing slash too", stem("/data/intake//"), "intake")
    check("the root has no name to stem", stem("/"), "")
    check("the empty path", stem(""), "")

    print "-- it takes what every other path builtin takes"
    ' The reference promises these work on a reference as well as a string, so
    ' a value straight out of a listing needs no conversion. A `stem` that only
    ' took text would read as working in every example above.
    f {file}= "/data/intake/vendor_2026-03.csv"
    d {dir}= "/data/intake"
    check("a file reference", stem(f), "vendor_2026-03")
    check("a directory reference", stem(d), "intake")
    ' Asserted on the TYPE as well, because `string()` of a file reference is
    ' its path, so a `stem` that handed the reference straight back would pass
    ' a comparison of rendered text. The finio amount defect exactly.
    check("the answer is text", type(stem(f)), "string")

    print "-- the composition this exists for"
    ' The ETL shape: take a name apart, change its suffix, put it back. Written
    ' with the three builtins rather than with `split`, which is what the
    ' stdlib hand-rolls in three places.
    src = "/data/intake/vendor_2026-03.csv"
    check("rename to a sibling with another suffix",
          join_path(directory_name(src), stem(src) + ".done"),
          "/data/intake/vendor_2026-03.done")
    check("the stem is the business key", stem(file_name(src)), "vendor_2026-03")

    print "-- an interior NUL is refused, by every name here"
    ' THE DOOR THE SAME FUNCTION LEFT OPEN FOR FOUR OF SIX NAMES. `real_path`
    ' and `file_type` have refused an interior NUL since they shipped, with the
    ' argument written three lines above the branch that did not: a path
    ' crosses into the system as a NUL-terminated C string, so a path with one
    ' inside would be CHECKED as one path and OPENED as another. That argument
    ' is about what a PATH is and not about which call is holding one.
    '
    ' Measured before the refusal was widened, and the answers are the reason
    ' this is a defect rather than an untidiness: with p = "/a/b\0x/c.txt",
    ' `file_name(p)` answered "b" -- the basename of `/a/b` -- `directory_name`
    ' answered "/a", and `join_path("/a\0b", "c")` answered "/a/c". Three
    ' ordinary-looking paths, none of them about the path the program held.
    nul = "/a/b" + chr(0) + "x/c.txt"
    on error goto next

    r1 = file_name(nul)
    if error then
        error.clear()
        check("file_name refuses", true, true)
    else
        check("file_name refuses", "answered " + quote(r1), true)
    end if

    r2 = directory_name(nul)
    if error then
        error.clear()
        check("directory_name refuses", true, true)
    else
        check("directory_name refuses", "answered " + quote(r2), true)
    end if

    r3 = extension(nul)
    if error then
        error.clear()
        check("extension refuses", true, true)
    else
        check("extension refuses", "answered " + quote(r3), true)
    end if

    r4 = stem(nul)
    if error then
        error.clear()
        check("stem refuses", true, true)
    else
        check("stem refuses", "answered " + quote(r4), true)
    end if

    ' BOTH arguments, because join_path has two and a rule applied to the first
    ' alone is a rule a caller walks around by swapping them.
    r5 = join_path(nul, "c")
    if error then
        error.clear()
        check("join_path refuses it on the left", true, true)
    else
        check("join_path refuses it on the left", "answered " + quote(r5), true)
    end if

    r6 = join_path("/a", "b" + chr(0) + "c")
    if error then
        error.clear()
        check("join_path refuses it on the right", true, true)
    else
        check("join_path refuses it on the right", "answered " + quote(r6), true)
    end if

    ' The two that already refused must STILL refuse -- the check was moved,
    ' and a move is the cheapest way to lose one.
    r7 = real_path(nul)
    if error then
        error.clear()
        check("real_path still refuses", true, true)
    else
        check("real_path still refuses", "answered " + quote(r7), true)
    end if

    r8 = file_type(nul)
    if error then
        error.clear()
        check("file_type still refuses", true, true)
    else
        check("file_type still refuses", "answered " + quote(r8), true)
    end if

    ' AND THE DOOR BEFORE THIS ONE, asserted rather than assumed: the check
    ' above tests a STRING, and a file or directory reference would walk past it
    ' if one could be built from a path with a NUL inside. Measured, neither
    ' modifier will build one -- so the string-only test is sufficient TODAY,
    ' and this pair is what stops that from quietly ceasing to be true.
    bad = "/a/b" + chr(0) + "x"
    r9 = {file}bad
    if error then
        error.clear()
        check("the file modifier refuses one at construction", true, true)
    else
        check("the file modifier refuses one at construction", "built it", true)
    end if

    r10 = {dir}bad
    if error then
        error.clear()
        check("the dir modifier refuses one at construction", true, true)
    else
        check("the dir modifier refuses one at construction", "built it", true)
    end if

    ' THE CONTROL, and without it every refusal above is satisfied by a path
    ' family that refuses everything -- which is worse than the defect, since
    ' these five are in the inner loop of any file walk.
    ordinary = "/a/b/c.txt"
    check("CONTROL: an ordinary path still splits",
          file_name(ordinary) + "|" + directory_name(ordinary) + "|" +
          extension(ordinary) + "|" + stem(ordinary) + "|" + join_path("/a", "b"),
          "c.txt|/a/b|txt|c|/a/b")
    ' AND the control has to reach a path that really is on disk, because
    ' real_path and file_type answer `unknown` for anything absent and would
    ' read as working on a build that refused every path they were given.
    check("CONTROL: real_path still resolves one that exists",
          file_name(real_path(".")) != "", true)
    check("CONTROL: file_type still answers for one that exists", file_type("."), "folder")

    print "-- a backslash is a CHARACTER here, and a separator on Windows"

    ' THE POSIX HALF OF A PLATFORM RULE, and the only half this machine can
    ' prove. The splitters read `path_is_sep`, which answers both separators on
    ' Windows and only `/` here -- because on Linux a backslash is an ORDINARY
    ' FILENAME CHARACTER and splitting on it would cut a real name in half.
    ' Measured: `touch "we\ird.txt"` succeeds on ext4.
    '
    ' The rule is not a translation and must not become one. Python is the
    ' nearest precedent and does the same thing: `os.path` IS `ntpath` on
    ' Windows, which reads both, while `posixpath.basename("weird\\name.txt")`
    ' keeps the backslash -- two implementations chosen by platform, not a
    ' conversion at the door. Measured 2026-10-17.
    '
    ' BOTH HALVES ARE ASSERTED, by asking which platform this is. The first
    ' version of this block asserted the POSIX answer unconditionally and said
    ' the Windows half "cannot be asserted from here" -- which was true of the
    ' VALUES and false of the file, since this fixture runs on Windows too
    ' (through MSYS bash) and failed there, correctly, reported from a Windows
    ' run 2026-10-17. A fixture that is wrong on one platform is worse than one
    ' that skips, and asking is better than either: `platform()` exists for
    ' exactly this, and asserting the Windows answer HERE means the rule is
    ' covered on the machine that implements it rather than only reported.
    ' THE WINDOWS BRANCH ASSERTS THE RULE, NOT VALUES I CANNOT MEASURE. The
    ' first draft of it asserted `join_path("a\\", "c")` is "a\\c", which is
    ' FALSE -- join_path READS both separators and always WRITES "/", so the
    ' answer there is "a/c". Asserting an unverified value about a platform this
    ' machine cannot run is the very mistake the Windows report caught in the
    ' draft above it, so what is asserted here is what follows from
    ' `path_is_sep` with certainty: a backslash either separates or it does not.
    if platform() = "windows" then
        check("a backslash DOES split a name there",
              file_name("we\\ird.txt") != "we\\ird.txt", true)
        check("...and names a parent", directory_name("a\\b\\c.txt") != ".", true)
    else
        check("a backslash does not split a name", file_name("we\\ird.txt"), "we\\ird.txt")
        check("...even after a real separator",
              file_name("dir/we\\ird.txt"), "we\\ird.txt")
        check("...and does not end a directory", directory_name("a\\b/c.txt"), "a\\b")
        check("join_path keeps it too", join_path("a\\b", "c"), "a\\b/c")
        check("...and a trailing one is not trimmed", join_path("a\\", "c"), "a\\/c")
        check("a path with no separator has no parent",
              directory_name("a\\b\\c.txt"), ".")
    end if
    ' PLATFORM-INVARIANT, and asserted on both sides of the branch rather than
    ' inside it: whatever join_path READS, it WRITES a forward slash. That is
    ' the half of the rule that makes `gb_realpath`'s normalisation and the temp
    ' bodies' join consistent with each other.
    check("join_path always writes a forward slash", join_path("a", "b"), "a/b")
    ' THE CONTROL that keeps the four above from being satisfied by splitters
    ' that stopped splitting: a real separator still cuts.
    check("CONTROL: a forward slash still splits", file_name("a/b.txt"), "b.txt")

    print "checks: " + string(tally.checks)
    print "mismatches: " + string(tally.mismatches)
end program
