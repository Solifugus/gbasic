' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' `byte_slice`, `byte_find`, `to_bytes` -- the byte-oriented family.
'
' WHY THEY EXIST. `byte_at` and `byte_count` shipped alone, and every other
' string builtin is CODEPOINT-oriented, so the two families did not compose: a
' binary-format reader that located a marker with `find` and then read it with
' `byte_at` got the WRONG BYTES, silently, with nothing raised.
'
' SELF-CHECKING RATHER THAN GOLDEN, and forced: every defect here is a
' PLAUSIBLE STRING. An index off by the width of one accented character still
' returns bytes, still prints, and is simply not the ones asked for.

function check(label, got, want)
    G.checks = G.checks + 1
    if string(got) = string(want) then
        print "ok   " + label
    else
        G.mismatches = G.mismatches + 1
        print "MISMATCH " + label + ": got " + string(got) + ", want " + string(want)
    end if
    return nothing
end function

program main(args)
    G = { checks: 0, mismatches: 0 }

    print "-- THE LOAD-BEARING TIER: the two families compose"
    ' A two-byte codepoint before the marker is all it takes: the codepoint
    ' index and the byte index part company, and only one of them is the one
    ' `byte_at` and `byte_slice` accept.
    s = from_bytes([ 0xC3, 0xA9 ]) + "MARK" + from_bytes([ 0xFF, 0xD8 ])
    check("the string is 8 bytes", byte_count(s), 8)
    check("but 7 codepoints", len(s), 7)
    check("find answers in CODEPOINTS", find(s, "MARK"), 1)
    check("byte_find answers in BYTES", byte_find(s, "MARK"), 2)
    ' This is the composition that was broken, and it is the whole reason for
    ' the family: the index one returns must be the index the others take.
    check("byte_find composes with byte_slice", byte_slice(s, byte_find(s, "MARK"), 4), "MARK")
    check("and with byte_at", byte_at(s, byte_find(s, "MARK")), 77)
    ' THE CONTROL, and it is what proves the tier is about composition rather
    ' than about byte_find alone: using `find`'s index gives the WRONG byte.
    check("CONTROL: find's index gives the wrong byte", byte_at(s, find(s, "MARK")), 169)

    print ""
    print "-- byte_slice"
    check("takes exactly the bytes asked for", hex_encode(byte_slice(s, 0, 2)), "c3a9")
    check("to the end when no count is given", hex_encode(byte_slice(s, 6)), "ffd8")
    check("a zero count is empty", byte_slice(s, 2, 0), "")
    ' Past the end is EMPTY and an over-long count is TRIMMED -- the same
    ' forgiving shape `mid` already has, so analogous calls behave alike.
    check("past the end is empty", byte_slice(s, 99, 4), "")
    check("an over-long count is trimmed", byte_count(byte_slice(s, 6, 99)), 2)
    check("the whole string round trips", byte_slice(s, 0, byte_count(s)) = s, true)

    print ""
    print "-- NUL is content, at a brand new byte-reading site"
    ' PLAT-NUL's standing lesson: a truncation defect found in one place is
    ' evidence about every place that reads a string.
    z = "a" + chr(0) + "b" + chr(0) + chr(0) + "c"
    check("byte_count sees all of it", byte_count(z), 6)
    check("byte_slice spans a NUL", byte_count(byte_slice(z, 0, 5)), 5)
    check("byte_find finds a NUL needle", byte_find(z, chr(0)), 1)
    check("and one from an offset", byte_find(z, chr(0), 2), 3)
    check("to_bytes keeps them", to_bytes(z), [97, 0, 98, 0, 0, 99])

    print ""
    print "-- to_bytes is the inverse of from_bytes"
    ' THE HIGH BYTES COME FIRST, deliberately. A signed/unsigned slip makes
    ' byte 200 read as -56, and `from_bytes` REFUSES a negative -- so a round
    ' trip placed first would RAISE and end the file, reporting an exit code
    ' where a named mismatch says which value went wrong.
    check("a high byte is not negative", to_bytes(from_bytes([200]))[0], 200)
    check("every value is a byte", to_bytes(from_bytes([0, 127, 128, 255])), [0, 127, 128, 255])
    every = ""
    i = 0
    while i < 256
        every = every + from_bytes([ i ])
        i = i + 1
    end while
    b = to_bytes(every)
    check("all 256 byte values survive", count(b), 256)
    check("and the last is 255", b[255], 255)
    check("round trip on binary", from_bytes(to_bytes(s)) = s, true)
    check("the empty string is an empty array", to_bytes(""), [])

    print ""
    print "-- byte_find"
    check("a miss answers nothing", string(byte_find(s, "zzz")), "nothing")
    check("a start past a hit skips it", byte_find("aXbXc", "X", 2), 3)
    check("a start past the end misses", string(byte_find("abc", "b", 99)), "nothing")
    check("a needle longer than the haystack misses", string(byte_find("ab", "abc")), "nothing")
    check("it finds at position zero", byte_find("abc", "a"), 0)

    print ""
    print "-- refusals, each beside its nearest legal neighbour"
    on error goto next
    byte_slice(s, -1, 2)
    check("a negative start is refused", contains(error.message, "may not be negative"), true)
    error.clear()
    byte_slice(s, 1.5, 2)
    check("a fractional start is refused", contains(error.message, "whole numbers"), true)
    error.clear()
    byte_slice(42, 0, 1)
    check("a non-string is refused", contains(error.message, "expects a string"), true)
    error.clear()
    byte_find(s, "")
    check("an empty needle is refused", contains(error.message, "needle is empty"), true)
    error.clear()
    byte_find(s, "M", -1)
    check("a negative start is refused there too", contains(error.message, "not negative"), true)
    error.clear()
    to_bytes(42)
    check("to_bytes refuses a non-string", contains(error.message, "expects a string"), true)
    error.clear()
    on error stop
    check("CONTROL: the ordinary calls still work", byte_slice("abcdef", 2, 3), "cde")

    print ""
    print "checks: " + string(G.checks)
    print "mismatches: " + string(G.mismatches)
end program
