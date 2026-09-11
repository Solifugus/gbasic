' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' A RECORD FIELD NAME IS COUNTED BYTES, like every other string in gBASIC.
'
' docs/reference.md has promised since the type was written that "any byte --
' including NUL -- is valid content; strings are not NUL-terminated from the
' program's point of view". PLAT-NUL made that true of string VALUES and left
' the one place a program can put arbitrary bytes into a NAME -- a dynamic key,
' `r[k] = v` -- reading them as a C string.
'
' THE DAMAGE WAS NOT TRUNCATION, IT WAS COLLAPSE. Measured before the fix:
'
'     r["a" + chr(0) + "b"] = 1
'     r["a" + chr(0) + "z"] = 2
'     -> ONE field, named "a", holding 2
'
' Two distinct keys became one, the second silently overwrote the first, and
' both subscripts read back the survivor. A record used as a map keyed by
' anything binary lost data with nothing raised.
'
' SELF-CHECKING NOT GOLDEN, and forced: every defect here produces a PLAUSIBLE
' RECORD. A collapsed pair still looks like a record and a truncated key still
' looks like a key, so a golden would have recorded one field where there
' should be two and then defended it.

tally = { checks: 0, mismatches: 0 }

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

k1 = "a" + chr(0) + "b"
k2 = "a" + chr(0) + "z"

print "-- two keys that differ only after a NUL are two keys"
' THE LOAD-BEARING CHECK, AND IT IS A COUNT. Asserting that r[k1] reads back 1
' is satisfied by a record that stored k1 alone; asserting there are TWO fields
' is what catches the collapse.
r = {}
r[k1] = 1
r[k2] = 2
check("both are stored", count(keys(r)), 2)
check("and each reads back its own value", string(r[k1]) + "," + string(r[k2]), "1,2")
check("the second did not overwrite the first", r[k1], 1)

print ""
print "-- the name comes back whole"
ks = keys(r)
check("keys() returns the full byte sequence", hex_encode(ks[0]), "610062")
check("and its length is the length that went in", len(ks[0]), 3)
check("a key read back from keys() finds its own field", r[ks[0]], 1)

print ""
print "-- membership answers about BYTES"
check("the whole name is present", has(r, k1), true)
' THE CONTROL, and it is the half that a library still carrying the bug would
' fail: under C-string semantics "a" and "a\0b" are the same name, so a record
' holding the second would report the first as present.
check("CONTROL: the prefix before the NUL is NOT a key", has(r, "a"), false)
check("nor is the text with the NUL removed", has(r, "ab"), false)

print ""
print "-- every other route a name travels"
d = remove_key(r, k1)
check("delete removes the one named, not its neighbour", count(keys(d)), 1)
check("and leaves the other whole", hex_encode(keys(d)[0]), "61007a")
m = merge(r, { plain: 9 })
check("merge keeps both and adds the third", count(keys(m)), 3)
' A record is a VALUE: writing through a copy must not reach the original, and
' the detach that makes that true copies every name.
c = r
c[k1] = 99
check("copy-on-write: the original is untouched", r[k1], 1)
check("and the copy took the write", c[k1], 99)

print ""
print "-- the hash index, which is a different code path"
' Below RECORD_INDEX_MIN_FIELDS a record is a linear walk; above it, a hash
' table. A length-blind hash puts every one of these in one bucket and a
' length-blind compare returns the first.
big = {}
i = 0
while i < 40
    big["k" + chr(0) + string(i)] = i
    i = i + 1
end while
check("forty NUL-bearing keys are forty fields", count(keys(big)), 40)
check("and each resolves to its own value", big["k" + chr(0) + "37"], 37)
check("a prefix is still not a key", has(big, "k"), false)

print ""
print "-- JSON: a NUL in a key is legal, and was being dropped"
' decode truncated the key at the NUL, so a well-formed document arrived with
' a corrupted key and nothing said so.
j = decode("{\"a\\u0000b\": 7}")
check("decode keeps the escaped NUL", hex_encode(keys(j)[0]), "610062")
check("and the value is reachable by the real key", j[k1], 7)
check("encode writes it back escaped", encode(r), "{\"a\\u0000b\":1,\"a\\u0000z\":2}")
check("and the round trip is the same record", count(keys(decode(encode(r)))), 2)

print ""
print "checks: " + string(tally.checks)
print "mismatches: " + string(tally.mismatches)
