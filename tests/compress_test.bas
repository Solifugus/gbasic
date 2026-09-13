' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' `compress` / `uncompress` -- deflate, reachable from gBASIC at last.
'
' SELF-CHECKING RATHER THAN GOLDEN, and forced: compressed output is opaque
' bytes. A golden would record whatever came out and defend it, and every
' defect here produces a PLAUSIBLE BLOB -- a stream in the wrong container, a
' level silently ignored, a length read with strlen so the tail is dropped.
' None of those looks wrong until something else tries to read it.

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
    tally = { checks: 0, mismatches: 0 }
    text = repeat("gBASIC business reporting. ", 200)

    print "-- a round trip, in all three containers"
    check("zlib round trips", uncompress(compress(text)), text)
    check("raw round trips", uncompress(compress(text, { format: "raw" }), { format: "raw" }), text)
    check("gzip round trips", uncompress(compress(text, { format: "gzip" }), { format: "gzip" }), text)
    check("and it actually compressed", len(compress(text)) < len(text), true)

    print ""
    print "-- THE LOAD-BEARING TIER: the format is a DIFFERENCE, not a label"
    ' Every check above is satisfied by an implementation that IGNORES `format`
    ' and always uses zlib -- it would compress and decompress consistently and
    ' round-trip perfectly. What separates the two is the bytes on the wire, so
    ' this asserts the containers are distinguishable AND not interchangeable.
    z = compress(text)
    r = compress(text, { format: "raw" })
    g = compress(text, { format: "gzip" })
    check("zlib carries its 0x78 header", hex_encode(left(z, 1)), "78")
    check("gzip carries its 1f8b magic", hex_encode(left(g, 2)), "1f8b")
    check("raw carries no header, so it is the shortest", len(r) < len(z), true)
    check("zlib and raw are different bytes", z = r, false)
    ' And the half that matters most: a stream read as the wrong container is
    ' REFUSED rather than half-decoded. PDF wants zlib, ZIP wants raw, and
    ' handing one to a reader expecting the other fails silently in C.
    on error goto next
    uncompress(r)
    check("raw read as zlib is refused", contains(error.message, "not a valid zlib"), true)
    error.clear()
    uncompress(z, { format: "gzip" })
    check("zlib read as gzip is refused", contains(error.message, "not a valid gzip"), true)
    error.clear()
    on error stop

    print ""
    print "-- binary safety: PLAT-NUL's lesson, at a brand new byte-reading site"
    ' Compressed data is FULL of NULs. A length read with strlen would truncate
    ' at the first one and hand back a shorter, perfectly plausible blob.
    every = ""
    i = 0
    while i < 256
        every = every + from_bytes([i])
        i = i + 1
    end while
    check("all 256 byte values survive a round trip", uncompress(compress(every)), every)
    nulls = chr(0) + "a" + chr(0) + chr(0) + "b" + chr(0)
    check("a value that is mostly NUL survives", uncompress(compress(nulls)), nulls)
    check("and keeps its length", len(uncompress(compress(nulls))), 6)
    check("the empty string round trips", uncompress(compress("")), "")

    print ""
    print "-- level is a difference too"
    check("9 beats 1 on compressible text", len(compress(text, { level: 9 })) < len(compress(text, { level: 1 })), true)
    check("0 stores, so it is bigger than the input", len(compress(text, { level: 0 })) > len(text), true)
    check("and level 0 still round trips", uncompress(compress(text, { level: 0 })), text)

    print ""
    print "-- the cap, because a stream does not declare its own size"
    bomb = compress(repeat(chr(65), 1000000))
    check("a megabyte of one byte compresses small", len(bomb) < 2000, true)
    on error goto next
    uncompress(bomb, { max_bytes: 1000 })
    check("and is refused against a small cap", contains(error.message, "expands past max_bytes"), true)
    error.clear()
    on error stop
    ' THE CONTROL. Without it "the cap refuses" is satisfied by a cap that
    ' refuses everything, which would make the builtin useless rather than safe.
    check("CONTROL: with room, the same stream expands fully", len(uncompress(bomb, { max_bytes: 2000000 })), 1000000)

    print ""
    print "-- refusals, each beside its nearest legal neighbour"
    on error goto next
    uncompress("not a compressed stream at all")
    check("garbage is refused", contains(error.message, "not a valid"), true)
    error.clear()
    uncompress(left(compress(text), 6))
    check("a truncated stream is refused", contains(error.message, "not a valid"), true)
    error.clear()
    compress("x", { fromat: "zlib" })
    check("an unknown option is refused BY NAME", contains(error.message, "unknown option 'fromat'"), true)
    error.clear()
    compress("x", { format: "bzip2" })
    check("an unknown format is refused", contains(error.message, "zlib, raw or gzip"), true)
    error.clear()
    compress("x", { level: 12 })
    check("a level out of range is refused", contains(error.message, "0 to 9"), true)
    error.clear()
    compress("x", { max_bytes: 10 })
    check("max_bytes is not a compress option", contains(error.message, "unknown option 'max_bytes'"), true)
    error.clear()
    uncompress(compress("x"), { level: 5 })
    check("level is not an uncompress option", contains(error.message, "unknown option 'level'"), true)
    error.clear()
    compress(42)
    check("a non-string is refused", contains(error.message, "expects a string"), true)
    error.clear()
    compress()
    check("no argument is refused", contains(error.message, "expects a string"), true)
    error.clear()
    on error stop
    check("CONTROL: a well-formed call still works", uncompress(compress("x", { level: 6, format: "zlib" })), "x")

    print ""
    print "checks: " + string(tally.checks)
    print "mismatches: " + string(tally.mismatches)
end program
