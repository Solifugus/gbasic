' pack, unpack and pack_size (docs/networking_design.md §8). Self-checking;
' run by tests/run_net.sh.
'
' Every expected byte string was produced by Python's struct module with the
' same format, not by this interpreter, and is written here as byte values so
' the check needs no hex builtin (which needs libcrypto).

tally = { checks: 0, mismatches: 0 }

function check(label, got, want)
    tally.checks = tally.checks + 1
    if type(got) = type(want) and string(got) = string(want) then
        print "ok   " + label
    else
        tally.mismatches = tally.mismatches + 1
        print "MISMATCH " + label + ": got " + string(got) + ", want " + string(want)
    end if
    return nothing
end function

function refusal(fmt, values)
    on error goto handle
    r = pack(fmt, values)
    return "ANSWERED"
handle:
    m = error.message
    error.clear()
    return m
end function

function unpack_refusal(fmt, bytes, at)
    on error goto handle
    r = unpack(fmt, bytes, at)
    return "ANSWERED"
handle:
    m = error.message
    error.clear()
    return m
end function

' The bytes of a string, as numbers.
function bytes_of(s)
    return to_bytes(s)
end function

print "-- PACK, against Python's struct --"
' struct.pack(">BIH", 81, 300, 7)
check(">B I H", bytes_of(pack(">B I H", [81, 300, 7])), [81, 0, 0, 1, 44, 0, 7])
' struct.pack("<BIH", 81, 300, 7)
check("<B I H, the same values little-endian", bytes_of(pack("<B I H", [81, 300, 7])), [81, 44, 1, 0, 0, 7, 0])
' struct.pack(">i", -2)
check(">i -2", bytes_of(pack(">i", [-2])), [255, 255, 255, 254])
' struct.pack(">h", -32768)
check(">h -32768", bytes_of(pack(">h", [-32768])), [128, 0])
' struct.pack(">q", -2)
check(">q -2", bytes_of(pack(">q", [-2])), [255, 255, 255, 255, 255, 255, 255, 254])
' struct.pack(">Q", 9007199254740993), one past what a double holds
check(">Q 2^53+1 stays exact", bytes_of(pack(">Q", [9007199254740993])), [0, 32, 0, 0, 0, 0, 0, 1])
' struct.pack(">3B", 1, 2, 3)
check("a count repeats a letter", bytes_of(pack(">3B", [1, 2, 3])), [1, 2, 3])
' struct.pack(">2sxB", b"ab", 9)
check("s and x", bytes_of(pack(">2s x B", ["ab", 9])), [97, 98, 0, 9])
check("the order can change mid-format", bytes_of(pack(">H <H", [1, 1])), [0, 1, 1, 0])
check("a NUL in an s field is content", bytes_of(pack(">3s", ["a" + chr(0) + "b"])), [97, 0, 98])

print ""
print "-- UNPACK --"
check("round trip >B I H", unpack(">B I H", pack(">B I H", [81, 300, 7])), [81, 300, 7])
check("<I 4294967295", unpack("<I", pack("<I", [4294967295])), [4294967295])
check("signed fields read signed", unpack(">b h i q", pack(">b h i q", [-1, -2, -3, -4])), [-1, -2, -3, -4])
check("unsigned fields read unsigned", unpack(">B H", pack(">b h", [-1, -1])), [255, 65535])
check("an offset reads from that byte", unpack(">H", "xx" + pack(">H", [513]), 2), [513])
check("s gives the bytes", unpack(">2s", "ab"), ["ab"])
check("x skips", unpack(">x B", pack(">2B", [7, 8])), [8])
check("2^53+1 reads back exact", unpack(">Q", pack(">Q", [9007199254740993]))[0], 9007199254740993)
check("pack_size", pack_size(">I 3H 5s x"), 16)

print ""
print "-- REFUSALS --"
check("no byte order", refusal("I", [1]),
      "pack: the format has no byte order; begin it with > (big-endian, network order) or < (little-endian)")
check("a value too big for its field", refusal(">B", [256]), "pack: value 1 (256) does not fit a B field")
check("a negative in an unsigned field", refusal(">H", [-1]), "pack: value 1 (-1) does not fit a H field")
check("the wrong number of values", refusal(">B B", [1]), "pack: the format takes 2 values, and 1 was given")
check("an s value of the wrong length", refusal(">5s", ["abc"]), "pack: value 1 is 3 bytes, and the field is 5s")
check("not a whole number", refusal(">I", [1.5]), "pack: value 1 is not a whole number an integer field can hold")
check("an unknown letter", contains(refusal(">z", [1]), "'z' is not a format letter"), true)
check("an s with no length", refusal(">s", ["a"]), "pack: an s field needs its length, as in 5s")
check("too few bytes to unpack", unpack_refusal(">I", "ab", 0),
      "unpack: the format needs 4 bytes at offset 0, and there are 2")
check("an offset past the end", unpack_refusal(">B", "ab", 5),
      "unpack: the format needs 1 bytes at offset 5, and there are 0")

print ""
print "checks: " + string(tally.checks)
print "mismatches: " + string(tally.mismatches)
