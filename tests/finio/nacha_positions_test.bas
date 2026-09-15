' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' THE FIRST EXTERNAL CHECK ON finio_nacha's LAYOUTS.
'
' Every other tier in this tree compares the adapter against fixtures this
' project generated, and those fixtures were built from the same understanding
' of the format the adapter reads them with -- so a layout wrong in both places
' agrees with itself perfectly and every suite stays green. The positions in
' tests/finio/nacha_positions.txt came from a field-level guide
' published by a bank to its own originating customers, which is a statement
' about the format that did not come from here.
'
' THE CONVERSION IS DONE HERE AND NOT IN THE FIXTURE, deliberately: the guide
' numbers from 1 and inclusively, `finio` counts from 0 with a length, and that
' is the single likeliest way to get a fixed-width layout wrong. Converting on
' the way into the fixture would move the off-by-one out of the code under test
' and into the data, where this tier could no longer see it.

load finio
load finio_nacha

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

f {file}= "tests/finio/nacha_positions.txt"
lines = split(read(f), chr(10))
seen = []
for each ln in lines
    body = trim(ln)
    if byte_count(body) > 0 and left(body, 1) != "#" then
        parts = []
        for each tok in split(body, " ")
            if byte_count(trim(tok)) > 0 then
                append(parts, trim(tok))
            end if
        end for
        kind = parts[0]
        append(seen, kind)
        lay = finio_nacha.layout_for(kind)
        check(kind + ": the guide lists as many fields as the layout defines",
              count(lay), count(parts) - 1)
        i = 1
        total = 0
        while i < count(parts) and i <= count(lay)
            span = split(parts[i], "-")
            ' 1-BASED INCLUSIVE -> 0-BASED OFFSET AND LENGTH, the one place this
            ' conversion is written.
            want_offset = number(span[0]) - 1
            want_length = number(span[1]) - number(span[0]) + 1
            fld = lay[i - 1]
            check((kind + " field " + string(i) + " (" + fld.concept + ") starts where the guide says"),
                  fld.offset, want_offset)
            check((kind + " field " + string(i) + " (" + fld.concept + ") is as long as the guide says"),
                  fld.length, want_length)
            total = total + want_length
            i = i + 1
        end while
        check(kind + ": the guide's own spans cover all 94 bytes", total, 94)
    end if
end for

' A COVERAGE FLOOR, because a tier that reads no lines passes by asserting
' nothing -- and this one reads its expectations from a file, which is exactly
' the shape that can silently stop finding them.
check("all six record layouts were checked against the guide", count(seen), 6)
for each k in finio_nacha.record_kinds()
    if k != "padding" then
        check("the guide covers " + k, contains(seen, k), true)
    end if
end for

print ""
print "checks: " + string(tally.checks)
print "mismatches: " + string(tally.mismatches)
