' THE SECOND EQUALITY AND THE NaN ORDER -- docs/unknown_nan_design.md,
' increment A. Self-checking; run by tests/run_same_value.sh.
'
' SQL has two equalities: `=` follows three-valued logic, and GROUP BY,
' DISTINCT and IN-list deduplication ask "is this the SAME VALUE" ("not
' distinct"). gBASIC's routes -- `contains`, `find`, `remove_value`, `unique`,
' a compound's deep comparison, a watcher's change test -- ask the second
' question, and so do the orderings: `sort` (plain and by field), `min`, `max`, `median`.
'
' THE ONE PLACE THE TWO DIFFER IS NaN, and the tiers below assert both halves:
' the routes treat NaN as the same value as NaN and place it after +inf, while
' the `=` and `<` OPERATORS keep IEEE's answer (decision D1). Either half alone
' passes on a build that changed the wrong one.

function check(label, got, want)
    tally.checks = tally.checks + 1
    if type(got) = type(want) and string(got) = string(want) then
        print "ok   " + label
    else
        tally.mismatches = tally.mismatches + 1
        print "MISMATCH " + label + ": got " + string(got) + " (" + type(got) + "), want " + string(want) + " (" + type(want) + ")"
    end if
    return nothing
end function

program main()
    tally = { checks: 0, mismatches: 0 }
    n = decode("nan")
    inf = decode("inf")

    print "-- THE ORDER: NaN is one value, after +inf, before an absence"
    ' The defect this fixes: a comparator answering "tie" for every pair
    ' touching a NaN is not an order, so the NaNs stopped the OTHER elements
    ' sorting -- [nan, 1, nan, 0] came back unchanged, 1 before 0.
    check("sort orders around NaN", sort([n, 1, n, 0]), [0, 1, n, n])
    check("NaN after +inf, absence last", sort([n, inf, unknown, -1, decode("-inf")]), [decode("-inf"), -1, inf, n, unknown])
    check("descending puts NaN first among numbers", sort([1, n, 2], { descending: true }), [n, 2, 1])
    check("sorting records by a field uses the same order", sort([{k: n}, {k: 2}, {k: 1}], { by: "k" }), [{k: 1}, {k: 2}, {k: n}])
    check("min skips past NaN", min([n, 1, 3]), 1)
    check("max is NaN, PostgreSQL's answer", string(max([n, 1, 3])), "nan")
    check("median picks by the same order", median([3, n, 1]), 3)
    ' Without NaN in the list the order is the one it always was: the control
    ' that says nothing else moved.
    check("an ordinary sort is unchanged", sort([3, -0.5, 2, 9007199254740993, 9007199254740992]), [-0.5, 2, 3, 9007199254740992, 9007199254740993])

    print ""
    print "-- THE ROUTES: NaN is the same value as NaN"
    check("contains finds a NaN", contains([1, n], n), true)
    check("find finds it", find([1, n], n), 1)
    check("unique keeps one NaN", unique([n, n, 1]), [n, 1])
    check("remove_value removes the first NaN", remove_value([n, 1, n], n), [1, n])
    check("a compound holding NaN equals itself", [n] = [n], true)
    check("and a record", {a: n} = {a: n}, true)

    print ""
    print "-- THE OPERATORS ARE UNCHANGED: IEEE (decision D1)"
    check("nan = nan is false", n = n, false)
    check("nan != nan is true", n != n, true)
    check("nan < 1 is false", n < 1, false)
    check("nan > 1 is false", n > 1, false)
    check("so not (nan > 1) and nan <= 1 still differ", not (n > 1) = (n <= 1), false)

    print ""
    print "-- AND THE ROUTES AGREE WITH THE OPERATOR EVERYWHERE ELSE"
    ' The controls. Without them "the routes treat NaN specially" is satisfied by
    ' routes that stopped asking the operator about anything.
    check("2 and 2.0 are one value", contains([1, 2], 2.0), true)
    check("0 and false: the coercion routes share with =", contains([0], false), true)
    check("0 and -0 are one value", [0.0] = [-0.0], true)
    check("unknown is the same value as unknown", unique([unknown, unknown]), [unknown])
    check("contains([unknown], unknown)", contains([unknown], unknown), true)
    check("a string is not a number", contains(["1"], 1), false)

    print ""
    print "-- EXACTNESS: a compound compared exactly, which it did not"
    ' Found writing this increment: the deep comparison compared DOUBLES, so
    ' two exact integers past 2^53 that round to one double were equal inside
    ' an array while the scalar comparison said they were not.
    check("scalar: 2^53 + 1 is not 2^53", 9007199254740993 = 9007199254740992, false)
    check("an array agrees", [9007199254740993] = [9007199254740992], false)
    check("a record agrees", {a: 9007199254740993} = {a: 9007199254740992}, false)
    check("contains agrees", contains([9007199254740992], 9007199254740993), false)
    check("unique keeps both", len(unique([9007199254740993, 9007199254740992])), 2)
    check("the control: equal exact compounds are equal", [9007199254740993] = [9007199254740993], true)

    print ""
    print "-- THE WATCHER: did the stored value change"
    w = { v: n, big: 9007199254740992 }
    fired = { nan: 0, big: 0 }
    watch(w.v)
        fired.nan = fired.nan + 1
    end watch
    watch(w.big)
        fired.big = fired.big + 1
    end watch
    before_nan = fired.nan
    before_big = fired.big
    w.v = decode("nan")
    check("NaN assigned over NaN is not a change", fired.nan, before_nan)
    w.big = 9007199254740993
    check("2^53 + 1 over 2^53 IS a change", fired.big > before_big, true)
    w.v = 1
    check("and 1 over NaN is a change (the control)", fired.nan > before_nan, true)

    print "ok   " + string(tally.checks) + " checks, " + string(tally.mismatches) + " mismatches"
end program
