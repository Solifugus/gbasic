' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' `for each item, i in list` -- the element AND its position.
'
' WHY IT EXISTS. gBASIC has no references and is not getting them: an actor is
' fork+exec so a reference cannot cross `spawn`, and `encode` totality is what
' lets an agent run sit in a store between HTTP requests -- references admit
' cycles and that property dies. The consequence is that a loop body cannot
' write through the element variable: `item.x = 1` mutates a COPY and is
' silently discarded. MEASURED across the whole tree, that mistake appears
' exactly once, in the fixture that asserts the semantics -- so the fix is not
' to change the semantics but to make the CORRECT idiom cheap. The index is
' what makes the write expressible, because `list[i] = item` is an lvalue PATH
' and paths write in place.
'
' SELF-CHECKING rather than golden: a loop that silently fails to write back
' leaves an array that still looks like an array, and a golden would record the
' unmodified values as expected.

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

print "-- the index is the position, 0-based like every other index"
seen = []
for each ch, i in ["x", "y", "z"]
    append(seen, string(i) + ch)
end for
check("index counts from 0", join(seen, ","), "0x,1y,2z")

print ""
print "-- THE POINT: a single-pass write-back"
' THE LOAD-BEARING TIER. Without the index this needs a hand-rolled counter and
' a `while`, which is why people reach for `for each` and silently lose the
' write. Asserted as a DIFFERENCE against the copy semantics below, since
' "the array changed" alone passes on a language with references and
' "the array did not change" alone passes on one where the loop does nothing.
nodes = [ { id: "a", n: 1 }, { id: "b", n: 2 } ]
for each item, i in nodes
    item.n = item.n * 10
    nodes[i] = item
end for
check("write-back through the index sticks", nodes[0].n, 10)
check("for every element", nodes[1].n, 20)

' THE CONTROL, and the reason the index is needed at all: the SAME loop
' WITHOUT the write-back changes nothing. If this ever starts passing, the
' language grew reference semantics and this whole design changed.
'
' `on warning ignore` because this loop is EXACTLY what warning 2107 exists to
' report -- a write to the element that nothing reads. Suppressing it here is
' not a workaround, it is the honest test that the opt-out works: a diagnostic
' whose own fixture cannot silence it is one that would have to be weakened
' instead. The scope is dynamic, so it is restored below.
on warning ignore
plain = [ { id: "a", n: 1 } ]
for each item, i in plain
    item.n = 999
end for
on warning stop
check("CONTROL: mutating the element alone is discarded", plain[0].n, 1)

print ""
print "-- writing to the array while walking it is SAFE"
' Iteration is over a SNAPSHOT: the array expression is evaluated once, and a
' write detaches the variable's store through copy-on-write. So a single pass
' that rewrites what it walks terminates and sees the original values --
' unlike Python, where mutating during iteration skips elements, or JavaScript,
' where it can loop forever.
walked = []
vals = [1, 2, 3]
for each v, i in vals
    append(walked, v)
    vals[i] = v * 100
end for
check("the walk saw the ORIGINAL values", string(walked), "[1,2,3]")
check("and the array holds the new ones", string(vals), "[100,200,300]")

grow = [1, 2]
rounds = 0
for each v, i in grow
    rounds = rounds + 1
    if rounds < 5 then
        append(grow, 99)
    end if
end for
check("appending inside the loop does NOT extend it", rounds, 2)
check("though the array really did grow", count(grow), 4)

print ""
print "-- the plain forms are untouched"
' The controls that say this is an addition rather than a change.
tot = 0
for each v in [1, 2, 3]
    tot = tot + v
end for
check("for each without an index still works", tot, 6)
tot2 = 0
for v in [1, 2, 3]
    tot2 = tot2 + v
end for
check("for ... in without an index still works", tot2, 6)
tot3 = 0
for j = 1 to 3
    tot3 = tot3 + j
end for
check("the counted loop still works", tot3, 6)

print ""
print "-- `for v, i in` -- the same form without `each`"
acc = 0
for v, j in [10, 20, 30]
    acc = acc + v * j
end for
check("index multiplies correctly", acc, 80)

print ""
print "-- flow control and closers"
sum = 0
for each v, i in [1, 2, 3]
    sum = sum + v + i
next v
check("`next <element>` closes the loop", sum, 9)

hit = []
for each v, i in [1, 2, 3, 4]
    if i = 1 then
        continue
    end if
    if i = 3 then
        break
    end if
    append(hit, v)
end for
check("continue and break work on the index", string(hit), "[1,3]")

n = 0
for each v, i in []
    n = n + 1
end for
check("an empty array iterates zero times", n, 0)

print ""
print "-- scope matches what the other loops already do"
' The index is an ordinary variable in the enclosing scope and survives the
' loop, exactly as the element and the counted loop's counter do. Asserted so
' that the index is not quietly a NEW kind of binding.
for each e in [1, 2, 3]
end for
for each e2, m in [1, 2, 3]
end for
check("the element survives, as it always did", e, 3)
check("and so does the index", m, 2)

print ""
print "-- nesting"
pairs = []
for each a, ai in ["p", "q"]
    for each b, bi in ["r", "s"]
        append(pairs, a + string(ai) + b + string(bi))
    end for
end for
check("nested indexed loops keep their own indices", join(pairs, " "), "p0r0 p0s1 q1r0 q1s1")

print ""
print "checks: " + string(tally.checks)
print "mismatches: " + string(tally.mismatches)
