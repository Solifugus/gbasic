' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' `read_at(f, position, count)` -- the other half of a door that only opened
' one way.
'
' `overwrite(f, text, position)` has written at a byte offset since it shipped
' and NOTHING COULD READ AT ONE: a program could modify the middle of a file it
' had no way to inspect, and the only reader was whole-file. That asymmetry is
' the argument for this builtin, and it is also why the load-bearing tier here
' is a ROUND TRIP THROUGH BOTH -- one offset space, two directions, which is
' the only check that fails if they disagree about where position 3 is.
'
' It is also the gap that made binary input unstreamable: `for each line in f`
' is the only bounded reader and a line is unbounded, so a 50 MB file with no
' newline byte in it is read as ONE 50 MB line. That half is measured by the
' runner, which can watch the memory from outside.
'
' SELF-CHECKING rather than golden: every defect here returns A PLAUSIBLE PIECE
' OF A FILE. An off-by-one offset gives four real bytes from the wrong place,
' which is exactly what a golden would record as expected.

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
    ' A SECOND DIRECTORY, ON THE REPOSITORY'S OWN FILESYSTEM, and it is not a
    ' convenience -- see the directory tier below. The work directory is a
    ' `mktemp -d`, which on this machine is TMPFS, and what `fseek`/`ftell`
    ' answer for a directory is a fact about the FILESYSTEM: on ext4 `ftell`
    ' says LONG_MAX, on tmpfs `fseek` fails outright. The tier that needs the
    ' LONG_MAX answer therefore cannot use the work directory at all.
    ondisk = args[1]
    tally = { checks: 0, mismatches: 0 }

    f {file}= root + "/ten.txt"
    write(f, "abcdefghij")

    print "-- the values, including every edge a reader meets"
    check("from the start", read_at(f, 0, 4), "abcd")
    check("from the middle", read_at(f, 3, 4), "defg")
    check("one byte", read_at(f, 5, 1), "f")
    ' A COUNT PAST THE END IS CLAMPED, not refused: that is what lets a caller
    ' pass a generous chunk size without asking the file's size first, which is
    ' the check-then-act shape that goes wrong while a file is still growing.
    check("a count past the end is clamped", read_at(f, 8, 50), "ij")
    check("a count far past the end still works", read_at(f, 0, 1000000), "abcdefghij")
    ' AT the end is the empty string, because that is how a reader TERMINATES.
    check("at the end", read_at(f, 10, 10), "")
    check("past the end", read_at(f, 99, 10), "")
    check("a zero count", read_at(f, 0, 0), "")
    check("an empty file", read_at({file}(root + "/empty.txt"), 0, 10), "")

    print "-- one offset space, two directions"
    ' THE LOAD-BEARING TIER. `overwrite` and `read_at` must agree about where
    ' position N is, and the only way to state that is to write at N through one
    ' and read at N through the other. An off-by-one in either would return four
    ' perfectly real bytes from the wrong place.
    g {file}= root + "/rt.txt"
    write(g, "0123456789")
    overwrite(g, "XY", 4)
    check("what overwrite put at 4 is what read_at finds at 4", read_at(g, 4, 2), "XY")
    check("and the bytes before it are untouched", read_at(g, 0, 4), "0123")
    check("and the bytes after it", read_at(g, 6, 4), "6789")
    ' Every position, both ways, so the agreement is not a fact about one offset.
    disagreements = 0
    i = 0
    while i < 10
        probe {file}= root + "/sweep.txt"
        write(probe, "..........")
        overwrite(probe, "Z", i)
        if read_at(probe, i, 1) != "Z" then
            disagreements = disagreements + 1
            print "  DISAGREE at position " + string(i)
        end if
        i = i + 1
    end while
    check("the two agree at every one of the ten positions", disagreements, 0)

    print "-- byte offsets, not codepoints, which is the other agreement"
    ' `read_at` is BYTE-positional, like `overwrite` and `byte_at` and unlike
    ' every codepoint builtin. A two-byte character is where that matters: the
    ' accent is bytes 0 and 1, so the letter after it starts at byte 2 while
    ' `mid` would call it character 1.
    acc {file}= root + "/acc.txt"
    write(acc, "é" + "X")
    check("the accent is two bytes", bytes(acc), 3)
    check("read_at 2 is the letter after it", read_at(acc, 2, 1), "X")
    check("and byte_at agrees with it",
          byte_at(read_at(acc, 0, 3), 2), byte_at("é" + "X", 2))
    ' And the codepoint route does NOT -- the control that makes the claim mean
    ' something rather than being a coincidence of ASCII.
    check("CONTROL: the codepoint route gives a different answer", mid("é" + "X", 1, 1), "X")

    print "-- a chunk of a binary file is bytes, NULs included"
    ' PLAT-NUL's rule applied to a new reader: a chunk of a zip or an image is
    ' full of NULs, and a C string would stop at the first one.
    b {file}= root + "/nul.bin"
    write(b, "ab" + chr(0) + "cd" + chr(0) + "ef")
    check("the file is eight bytes", bytes(b), 8)
    whole = read_at(b, 0, 8)
    check("and read_at returns all eight", len(whole), 8)
    check("with the NULs where they were", hex_encode(whole), "6162006364006566")
    check("a chunk that STARTS with a NUL is two bytes", len(read_at(b, 2, 2)), 2)
    ' Reassembly: chunked reads must rebuild the file exactly, which is the
    ' property every consumer of this builtin depends on.
    rebuilt = ""
    pos = 0
    while true
        piece = read_at(b, pos, 3)
        if piece = "" then
            break
        end if
        rebuilt = rebuilt + piece
        ' BYTE_COUNT, NOT LEN -- see the tier below, which is the whole reason
        ' this line is worth a comment.
        pos = pos + byte_count(piece)
    end while
    check("three-byte chunks rebuild the file exactly", hex_encode(rebuilt), hex_encode(read(b)))

    print "-- the trap: a position is BYTES and `len` is CODEPOINTS"
    ' THE SHARPEST THING TO KNOW ABOUT THIS BUILTIN, and it was found by this
    ' suite failing rather than by writing it down: the obvious reader loop is
    ' `pos = pos + len(chunk)`, and `len` counts CODEPOINTS while `read_at`
    ' takes BYTES. On text they agree; on anything else the loop advances short,
    ' re-reads what it already had, and reassembles a LONGER, corrupted file --
    ' silently.
    '
    ' Measured on 64 KB of random bytes: byte_count 65536, len 62800. The first
    ' version of this fixture could not see it, because its own data was ASCII
    ' and NULs -- both single-byte codepoints, where the two agree. COMFORTABLE
    ' TEST DATA HID IT, the same way a hash bug hid behind long keys in
    ' recipe 7.
    '
    ' Stated here deterministically with a two-byte character rather than with
    ' random data, so the tier asserts an exact figure instead of a probability.
    m {file}= root + "/multi.txt"
    wide = ""
    i2 = 0
    while i2 < 50
        wide = wide + "é"
        i2 = i2 + 1
    end while
    write(m, wide)
    check("the file is 100 bytes", bytes(m), 100)
    check("and 50 codepoints", len(read(m)), 50)
    piece64 = read_at(m, 0, 64)
    check("a 64-byte chunk really is 64 bytes", byte_count(piece64), 64)
    check("but len() calls it 32", len(piece64), 32)

    ' AND THE DIFFERENCE THE TRAP MAKES, which is what a value check alone
    ' cannot show: the same loop written both ways over the same file.
    by_bytes = ""
    pos = 0
    while true
        piece = read_at(m, pos, 64)
        if piece = "" then
            break
        end if
        by_bytes = by_bytes + piece
        pos = pos + byte_count(piece)
    end while
    by_len = ""
    pos = 0
    guard = 0
    while guard < 20
        piece = read_at(m, pos, 64)
        if piece = "" then
            break
        end if
        by_len = by_len + piece
        pos = pos + len(piece)
        guard = guard + 1
    end while
    check("advancing by byte_count rebuilds the file", byte_count(by_bytes), 100)
    check("advancing by len does NOT -- it re-reads and overshoots",
          byte_count(by_len) != 100, true)

    print "-- a generous count must not become a generous allocation"
    ' THE CLAMP IS A SAFETY PROPERTY, NOT A TIDINESS ONE, and a perturbation
    ' showed why no value check can see it: removing it leaves every answer
    ' CORRECT -- `fread` reads only what is there -- and changes only how much
    ' was allocated to hold it. What makes it assertable is a count large
    ' enough that the allocation FAILS: `malloc` failure in this interpreter is
    ' `abort()`, which is the SIGABRT-and-core-dump defect fixed in the
    ' whole-file readers one day before this builtin shipped. A directory is
    ' the same hazard by another door, reporting LONG_MAX as its size.
    '
    ' So this asks for nine quintillion bytes of a ten-byte file and requires
    ' the ten bytes back. Unclamped, the process does not survive the line.
    check("nine quintillion bytes of a ten-byte file is ten bytes",
          read_at(f, 0, 9000000000000000000), "abcdefghij")
    check("and from the middle", read_at(f, 7, 9000000000000000000), "hij")

    print "-- the refusals, each beside the call that must still work"
    on error goto next

    r = read_at(f, -1, 4)
    refused("a negative position is refused", error)
    if error then
        error.clear()
    end if

    r = read_at(f, 0, -4)
    refused("a negative count is refused", error)
    if error then
        error.clear()
    end if

    r = read_at(f, 1.5, 4)
    refused("a fractional position is refused", error)
    if error then
        error.clear()
    end if

    r = read_at(f, 0, 2.5)
    refused("a fractional count is refused", error)
    if error then
        error.clear()
    end if

    r = read_at(f, 0)
    refused("two arguments are refused -- it takes three", error)
    if error then
        error.clear()
    end if

    r = read_at(root + "/ten.txt", 0, 4)
    refused("a plain string is refused -- it takes a file reference", error)
    if error then
        error.clear()
    end if

    r = read_at({file}(root + "/missing.txt"), 0, 4)
    refused("a file that is not there is refused", error)
    if error then
        error.clear()
    end if

    ' A DIRECTORY, which the whole-file readers learned to refuse the day
    ' before this shipped -- and a third reader must not reintroduce the crash.
    r = read_at({file}root, 0, 4)
    refused("a directory is refused", error)
    if error then
        m = error.message
        error.clear()
        check("and it says so by name", contains(m, "Is a directory"), true)
    end if

    ' AND THE CASE THAT MAKES THE GUARD LOAD-BEARING RATHER THAN BELT AND
    ' BRACES, which was found by perturbation: with the `fstat` check removed,
    ' a small count still refuses with the IDENTICAL message, because `fread`
    ' on a directory fails with EISDIR and the error path reports it. Measured
    ' both ways -- so for the MESSAGE the guard is redundant.
    '
    ' What it actually protects is the ALLOCATION. A directory's `ftell`
    ' answers LONG_MAX, so the clamp has nothing to clamp against, and the
    ' count is allocated as asked: unguarded, this line is SIGABRT and a core
    ' dump rather than a refusal -- the same defect, in a third reader, that
    ' the whole-file ones were fixed for a day earlier.
    '
    ' IT MUST BE A DIRECTORY ON THE REPOSITORY'S FILESYSTEM, which is why this
    ' fixture takes a second argument. The perturbation that disables the guard
    ' passed for an hour because the tier used the work directory: that is a
    ' `mktemp -d` under TMPFS, where `fseek` on a directory FAILS and the read
    ' is refused long before any allocation. On ext4 the same call answers
    ' LONG_MAX and dies. Measured both ways -- the tier was green against a
    ' build that crashes, on a machine where the crash reproduces by hand.
    r = read_at({file}ondisk, 0, 9000000000000000000)
    refused("a directory with a generous count is refused, not a core dump", error)
    if error then
        error.clear()
    end if

    ' THE CONTROL for the whole block: without it every refusal above is
    ' satisfied by a builtin that refuses everything.
    check("CONTROL: an ordinary call still reads", read_at(f, 1, 3), "bcd")

    print "checks: " + string(tally.checks)
    print "mismatches: " + string(tally.mismatches)
end program
