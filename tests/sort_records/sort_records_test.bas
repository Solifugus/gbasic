' `sort(rows, { by: ..., descending: ... })` -- ordering a list of RECORDS,
' which is what business data is.
'
' `sort` refused records outright before this (`sort supports only scalar array
' values`), and the consequence was in shipped stdlib: MEASURED, four
' hand-rolled sorts over records --
'
'   stdlib/frame.bas:340        one field ascending, O(n^2) insertion sort
'   stdlib/fundamentals.bas:183 TWO fields, faked as r["end"] + "|" + r["start"]
'   stdlib/nlq.bas:1507         score DESCENDING then id ASCENDING
'   stdlib/stats.bas:3438,8313  an INDEX array against a parallel column
'
' -- and the third is why `descending` takes two shapes. A plain boolean turns
' every key around, which covers the first two and NOT the third, whose own
' comment says why its total order matters. So `descending` is a boolean (every
' key) or a LIST OF FIELD NAMES (those keys).
'
' SELF-CHECKING, NOT GOLDEN, and forced: every defect here is a PLAUSIBLE ORDER.
' A sort that honours only the first key, or turns the wrong one around, or loses
' ties to an unstable merge, produces a list that still looks sorted -- and a
' golden would record it as expected.

function check(label, got, want)
    if string(got) = string(want) then
        print "ok   " + label
    else
        print "MISMATCH " + label + ": got " + string(got) + ", want " + string(want)
    end if
    return nothing
end function

' AN ORACLE WRITTEN IN gBASIC: a plainly-correct insertion sort over the same
' keys, which is a SECOND IMPLEMENTATION rather than a second call into the one
' under test. That is what catches a merge sort which is self-consistently
' wrong -- every value check in this file would pass on one.
function oracle_less(a, b, keys, desc)
    for each k in keys
        av = a[k]
        bv = b[k]
        if string(av) != string(bv) then
            ' Absences rank below every ordinary value, matching core `sort`.
            if is_unknown(av) then
                lt = true
            else
                if is_unknown(bv) then
                    lt = false
                else
                    lt = av < bv
                end if
            end if
            if contains(desc, k) then
                return not lt
            end if
            return lt
        end if
    end for
    return false
end function

function oracle_sort(rows, keys, desc)
    out = []
    for each r in rows
        placed = false
        nxt = []
        i = 0
        while i < count(out)
            if not placed then
                if oracle_less(r, out[i], keys, desc) then
                    append(nxt, r)
                    placed = true
                end if
            end if
            append(nxt, out[i])
            i = i + 1
        end while
        if not placed then
            append(nxt, r)
        end if
        out = nxt
    end for
    return out
end function

program main( args )
    rows = [ { last: "Smith", first: "Zoe", amount: 30 },
             { last: "Adams", first: "Bob", amount: 10 },
             { last: "Smith", first: "Al",  amount: 20 } ]

    print "-- by one field"
    check("one field                ", sort(rows, { by: "last" }).first, ["Bob", "Zoe", "Al"])
    check("a number field           ", sort(rows, { by: "amount" }).amount, [10, 20, 30])

    print "-- by several fields, which is what string concatenation was faking"
    check("two fields               ", sort(rows, { by: ["last", "first"] }).first, ["Bob", "Al", "Zoe"])

    print "-- descending: a boolean turns every key around"
    check("descending true          ", sort(rows, { by: "amount", descending: true }).amount, [30, 20, 10])
    check("both keys descend        ", sort(rows, { by: ["last", "first"], descending: true }).first, ["Zoe", "Al", "Bob"])

    print "-- descending: a LIST names which keys descend (the nlq shape)"
    ' THE CASE A BOOLEAN CANNOT EXPRESS, and the one with a bug-shaped comment
    ' attached in shipped stdlib: score descending, id ascending.
    scored = [ { s: 5, id: "c" }, { s: 9, id: "a" },
               { s: 5, id: "a" }, { s: 9, id: "b" } ]
    mixed = sort(scored, { by: ["s", "id"], descending: ["s"] })
    check("mixed direction          ", mixed.s, [9, 9, 5, 5])
    check("and the minor key ascends", mixed.id, ["a", "b", "a", "c"])

    print "-- THE ORACLE: a second implementation must agree"
    check("one field vs oracle      ", sort(rows, { by: "last" }), oracle_sort(rows, ["last"], []))
    check("two fields vs oracle     ", sort(rows, { by: ["last","first"] }), oracle_sort(rows, ["last","first"], []))
    check("mixed vs oracle          ", sort(scored, { by: ["s","id"], descending: ["s"] }), oracle_sort(scored, ["s","id"], ["s"]))
    ' A WIDER BODY OF DATA, so the oracle is not agreeing on three rows by luck.
    many = []
    i = 0
    while i < 40
        append(many, { g: mod(i, 4), h: mod(i * 7, 5), n: i })
        i = i + 1
    end while
    check("40 rows vs oracle        ", sort(many, { by: ["g","h"] }), oracle_sort(many, ["g","h"], []))
    check("40 rows mixed vs oracle  ", sort(many, { by: ["g","h"], descending: ["h"] }), oracle_sort(many, ["g","h"], ["h"]))

    print "-- STABLE, which is a portability requirement, not a nicety"
    ' `qsort` is not stable and its tie order differs between implementations, so
    ' a golden pinning rows sorted by one field would read differently on glibc
    ' and on a BSD libc. Asserted as a DIFFERENCE: the same ties in a DIFFERENT
    ' entry order must come out in that order, or "stable" is satisfied by any
    ' deterministic rule at all.
    ties_a = [ { g: "b", id: 1 }, { g: "a", id: 2 }, { g: "b", id: 3 }, { g: "a", id: 4 } ]
    ties_b = [ { g: "b", id: 3 }, { g: "a", id: 4 }, { g: "b", id: 1 }, { g: "a", id: 2 } ]
    check("ties keep entry order    ", sort(ties_a, { by: "g" }).id, [2, 4, 1, 3])
    check("and a different entry one", sort(ties_b, { by: "g" }).id, [4, 2, 3, 1])

    print "-- what stability BUYS: the two-pass remedy for an unmixable sort"
    ' `descending` cannot be per-key on a BOOLEAN, and the remedy for anything
    ' this record shape still cannot express is to sort by the minor key and then
    ' by the major one -- which is correct ONLY because the sort is stable. The
    ' two must give the identical answer, which is the whole claim.
    two = [ { s: 5, id: "c" }, { s: 9, id: "a" }, { s: 5, id: "a" }, { s: 9, id: "b" } ]
    sort(two, { by: "id" })
    sort(two, { by: "s", descending: true })
    check("two passes = one declare ", two, mixed)

    print "-- a plain list gains `descending`, which it never had"
    ' Before this the only spelling was `reverse(sort(xs))`, two passes over the
    ' array to say one thing.
    check("scalar descending        ", sort([3, 1, 2], { descending: true }), [3, 2, 1])
    check("and ascending by default ", sort([3, 1, 2], {}), [1, 2, 3])
    check("text descending          ", sort(["a","c","b"], { descending: true }), ["c", "b", "a"])
    check("money descending         ", string(sort([{USD}"1.00", {USD}"3.00"], { descending: true })), "[3.00,1.00]")

    print "-- IN PLACE, which adding options must not have changed"
    inplace = [ { n: 2 }, { n: 1 } ]
    r = sort(inplace, { by: "n" })
    check("mutates the caller's list", inplace.n, [1, 2])
    check("and answers it too       ", r.n, [1, 2])
    plain = [3, 1, 2]
    sort(plain)
    check("one argument unchanged   ", plain, [1, 2, 3])

    print "-- an absent key is an absence, ranked as `sort` already ranks one"
    holes = [ { x: 3 }, { x: unknown }, { x: 1 }, { y: "no x at all" } ]
    check("absent and unknown alike ", sort(holes, { by: "x" }).x, [unknown, unknown, 1, 3])
    check("scalar sort agrees       ", sort([3, unknown, 1]), [unknown, 1, 3])

    print "-- REFUSALS"
    on error goto next
    x = sort(rows, { bye: "last" })
    check("unknown option by name   ", contains(error.message, "unknown option 'bye' (known: by, descending)"), true)
    error.clear()
    x = sort(rows, { by: "last", descending: ["amount"] })
    check("descending names a non-key", contains(error.message, "names 'amount', which `by` does not sort on"), true)
    error.clear()
    x = sort([1, 2], { by: "g" })
    check("by over a plain list     ", contains(error.message, "sort by `g` expects every element to be a record; element 0 is a number"), true)
    error.clear()
    x = sort(rows, "last")
    check("options must be a record ", contains(error.message, "expects an options record"), true)
    error.clear()
    x = sort(rows, { by: 7 })
    check("by must name a field     ", contains(error.message, "`by` names a field or a list of fields, got a number"), true)
    error.clear()
    x = sort(rows, { by: [] })
    check("by may not be empty      ", contains(error.message, "`by` is an empty list"), true)
    error.clear()
    x = sort(rows, { descending: ["last"] })
    check("descending needs a by    ", contains(error.message, "there is no `by` to name them in"), true)
    error.clear()
    x = sort(rows, { by: "last", descending: 7 })
    check("descending is bool or list", contains(error.message, "true/false, or a list of the fields that descend"), true)
    error.clear()
    ' THE KEY COLUMN GOES THROUGH THE ORDERING GATE, the same one the array
    ' itself goes through -- so a key that cannot be ordered is refused in the
    ' gate's own words, and the message NAMES THE COLUMN.
    cross = [ { t: {USD}"1.00" }, { t: {EUR}"2.00" } ]
    x = sort(cross, { by: "t" })
    check("key: mixed currencies    ", contains(error.message, "sort by `t` cannot order money in different currencies (USD and EUR)"), true)
    error.clear()
    mt = [ { a: 1 }, { a: "x" } ]
    x = sort(mt, { by: "a" })
    check("key: two ordinary types  ", contains(error.message, "sort by `a` requires ordinary values to have the same type"), true)
    error.clear()
    ' EVERY KEY IS GATED, not just the first -- a gate that checked only key 0
    ' would let a second key through to a comparator that cannot raise, and the
    ' message must name the SECOND column.
    second = [ { a: 1, b: 2 }, { a: 2, b: "x" } ]
    x = sort(second, { by: ["a", "b"] })
    check("key: the second one too  ", contains(error.message, "sort by `b` requires"), true)
    error.clear()
    ' CONTROLS, or every refusal above is satisfied by a sort that refuses
    ' everything with a second argument.
    check("one currency is fine     ", string(sort([{ t: {USD}"3.00" }, { t: {USD}"1.00" }], { by: "t" }).t), "[1.00,3.00]")
    check("one type is fine         ", sort([{ a: 2 }, { a: 1 }], { by: "a" }).a, [1, 2])
    ' ABSENT OPTIONS BEHAVE EXACTLY LIKE ONE ARGUMENT, which is the control that
    ' says the second argument changes nothing when there is nothing in it: over
    ' a plain list it sorts, and over RECORDS it refuses in the same words
    ' `sort(rows)` has always used -- `nothing` is not an empty `by`.
    check("absent options: scalars  ", sort([3, 1, 2], nothing), [1, 2, 3])
    error.clear()
    x = sort(rows, nothing)
    check("absent options: records  ", contains(error.message, "sort supports only scalar array values"), true)
    error.clear()
    x = sort(rows)
    check("and one argument agrees  ", contains(error.message, "sort supports only scalar array values"), true)
    ' Claimed, or PLAT-ERR rule 2 re-raises it at the end of the program -- which
    ' is the anti-silence rule working, and would report this fixture as failing.
    error.clear()
end program
