' `rows.amount` -- PROJECTION: the array of that field from every element.
'
' THE BLOCKING PRIMITIVE FOR WORKING WITH DATA IN BULK, and smaller than `map`,
' `filter` or `sort`. MEASURED across stdlib, examples and tests: of 813
' `for each` loops, 22 are a sum over the loop variable and EVERY ONE is
' `total = total + r.amount` -- summing a FIELD. `sum`, `mean`, `count`, `min`
' and `max` already existed and could not reach data, because nothing could name
' a field across a list.
'
' THE SYNTAX IS SAFE BY PROOF, NOT BY SURVEY: every dotted access on an array
' raised before this, so no working program could contain the shape.
'
' SELF-CHECKING, and the ORACLE is the loop it replaces -- the same figure
' computed both ways must agree. That is the check a projection which silently
' DROPS or REORDERS elements fails, and every value check in this file would
' pass on one that drops absences.

function check(label, got, want)
    if string(got) = string(want) then
        print "ok   " + label
    else
        print "MISMATCH " + label + ": got " + string(got) + ", want " + string(want)
    end if
    return nothing
end function

program main( args )
    rows = [ { item: "hammer", amount: 19.95, paid: true },
             { item: "saw",    amount: 32.50, paid: false },
             { item: "nails",  amount:  7.00, paid: true } ]

    print "-- THE ORACLE: the loop it replaces must agree"
    total = 0
    for each r in rows
        total = total + r.amount
    next
    check("sum matches the loop     ", sum(rows.amount), total)
    ' AND THE COUNT, which is what catches a projection that drops elements --
    ' every figure above can be right while an element is missing.
    check("count is every element   ", count(rows.amount), count(rows))

    print "-- the five aggregates projection unlocks"
    check("sum                      ", sum(rows.amount), 59.45)
    check("mean                     ", round(mean(rows.amount), 4), 19.8167)
    check("min                      ", min(rows.amount), 7)
    check("max                      ", max(rows.amount), 32.5)
    check("count                    ", count(rows.amount), 3)

    print "-- ORDER IS PRESERVED, which no aggregate above can see"
    check("order                    ", rows.amount, [19.95, 32.5, 7])
    check("a text field             ", rows.item, ["hammer", "saw", "nails"])

    print "-- absence: a missing field is `unknown` for that element"
    mixed = [ { a: 1 }, { b: 2 }, { a: 3 } ]
    check("missing field            ", mixed.a, [1, unknown, 3])
    check("and the element is kept  ", count(mixed.a), 3)

    ' ABSENT AND EXPLICITLY EMPTY ARE DIFFERENT CLAIMS and the projection keeps
    ' them apart: `unknown` means the row has no such field, `nothing` means the
    ' field is there and holds nothing. Collapsing them would be a plausible
    ' simplification and would lose the distinction every absence rule rests on.
    held = [ { a: nothing }, { a: 1 } ]
    check("explicit nothing kept    ", held.a, [nothing, 1])
    check("and is not the same as   ", string(mixed.a) != string([1, nothing, 3]), true)
    ' AN EMPTY LIST PROJECTS TO AN EMPTY LIST, not a refusal.
    check("empty list               ", [].a, [])

    print "-- nesting falls out of applying the rule twice"
    deep = [ { c: { n: "x" } }, { c: { n: "y" } } ]
    check("rows.c.n                 ", deep.c.n, ["x", "y"])

    print "-- min/max over any orderable scalar, not just numbers"
    ' `sort` has always ordered strings and dates while `max(["a","b"])` refused.
    ' The same gate and the same comparator as `sort`, so the two cannot
    ' disagree about what is orderable.
    due = [ { name: "saw", on: {date}"2026-03-07" },
            { name: "hammer", on: {date}"2025-01-01" } ]
    check("max of text              ", max(due.name), "saw")
    check("min of text              ", min(due.name), "hammer")
    check("max of dates             ", string(max(due.on)), "2026-03-07")
    check("min of dates             ", string(min(due.on)), "2025-01-01")

    print "-- MONEY, which is what a business row actually holds"
    ' THE FIVE AGGREGATES §1 UNLOCKS HAD TO REACH MONEY OR THE UNLOCK MISSES
    ' INVOICES. Before this increment every one of them refused a money array --
    ' `sum`/`mean` with "expects a numeric array", `min`/`max`/`sort` with
    ' "supports only scalar array values" -- so `sum(invoices.total)`, the most
    ' obvious expression in a business language, did not work.
    inv = [ { who: "a", total: {USD}"32.50" },
            { who: "b", total: {USD}"7.00" },
            { who: "c", total: {USD}"19.95" } ]
    check("sum of money             ", string(sum(inv.total)), "59.45")
    check("min of money             ", string(min(inv.total)), "7.00")
    check("max of money             ", string(max(inv.total)), "32.50")
    check("sort of money            ", string(sort(inv.total)), "[7.00,19.95,32.50]")
    check("count of money           ", count(inv.total), 3)
    ' RETENTION IS THE TIER DISPLAY CANNOT SHOW, the lesson run_money records:
    ' 59.45/3 is 19.816666..., which DISPLAYS as 19.82 whether it was kept
    ' exactly or rounded at the door, so only arithmetic separates them.
    ' MEASURED by perturbation rather than reasoned: a `mean` that ROUNDS at the
    ' minor unit prints 19.82, identical to the correct answer, and only
    ' `avg * 3` shows it -- 59.46 instead of 59.45. A fixture asserting the
    ' displayed form alone would have passed on that binary.
    avg = mean(inv.total)
    check("mean of money displays   ", string(avg), "19.82")
    check("and keeps the sub-cent   ", string(avg * 3), "59.45")

    print "-- DURATION, the other kind `<` ordered and `sort` refused"
    spans = [ { job: "a", took: 3 days }, { job: "b", took: 1 hour },
              { job: "c", took: 2 days } ]
    check("sort of durations        ", string(sort(spans.took)), "[1 hour,2 days,3 days]")
    check("max of durations         ", string(max(spans.took)), "3 days")
    check("min of durations         ", string(min(spans.took)), "1 hour")

    print "-- any / all, which need no predicate once projection exists"
    check("any                      ", any(rows.paid), true)
    check("all                      ", all(rows.paid), false)
    check("all over all-true        ", all([{p:true},{p:true}].p), true)
    ' VACUOUS TRUTH, the standard convention, pinned so it cannot drift.
    check("any of nothing           ", any([]), false)
    check("all of nothing           ", all([]), true)
    ' AN ABSENCE IS NOT TRUE: a row that never said it was paid has not been paid.
    partial = [ { paid: true }, { item: "no flag" } ]
    check("all with an absence      ", all(partial.paid), false)
    check("any with an absence      ", any(partial.paid), true)

    print "-- REFUSALS"
    on error goto next
    bad = [ { a: 1 }, 7 ]
    z = bad.a
    check("a non-record element     ", contains(error.message, "element 1 is a number"), true)
    error.clear()
    ' AND THE ARTICLE IS RIGHT, because "is a array" reads as a mistake in the
    ' sentence a beginner meets. The kind names are a closed set, so this is a
    ' one-character test rather than a table.
    nested = [ { a: 1 }, [2] ]
    z2 = nested.a
    check("and says `an array`      ", contains(error.message, "element 1 is an array"), true)
    error.clear()
    ' A PROJECTION IS A VALUE, NOT A PLACE. The five mutating builtins must go on
    ' refusing -- appending to a derived array is meaningless, and silently
    ' appending to a throwaway copy would be worse than the refusal.
    append(rows.amount, 1)
    check("append refused           ", contains(error.message, "cannot change a projection"), true)
    error.clear()
    remove(rows.amount, 0)
    check("remove refused           ", contains(error.message, "cannot change a projection"), true)
    error.clear()
    ' AND THE MESSAGE NAMES THE VERB THE AUTHOR WROTE. `append` and `prepend`
    ' share one code path, so a first version of this said "prepend" for an
    ' `append` call -- the verb comes from the call site now.
    append(rows.amount, 1)
    check("and names the verb       ", contains(error.message, "append cannot"), true)
    error.clear()
    ' `any`/`all` TAKE BOOLEANS, and the refusal must name a remedy THAT EXISTS.
    ' A first version said "compare it first -- any(rows.amount > 0)", which does
    ' NOT work: a projection is an array and PLAT-EQ refuses ordering on arrays.
    q = any(rows.item)
    check("any refuses non-boolean  ", contains(error.message, "takes a boolean field"), true)
    check("and promises no bad remedy", contains(error.message, "rows.amount > 0"), false)
    error.clear()
    ' AN ARRAY WITH HOLES still refuses, consistently across aggregates, until
    ' the absence rules land with their warning. `sort`'s comparator ranks
    ' `unknown` below every ordinary value, so routing one through it made `max`
    ' answer while `sum` refused -- the skip-the-absence rule arriving early and
    ' silently. Pinned so it cannot arrive that way again.
    m = max(mixed.a)
    check("max over holes refuses   ", contains(error.message, "numeric array"), true)
    error.clear()
    s2 = sum(mixed.a)
    check("sum over holes refuses   ", contains(error.message, "numeric array"), true)
    ' AND IT NAMES THE ELEMENT, which over a projection is the whole question --
    ' an array of absences means a ROW is missing the field, and `sum expects a
    ' numeric array` alone leaves the author to find which. It also removed an
    ' asymmetry this increment introduced: `[{USD}"1.00", 7]` named element 1
    ' while `[7, {USD}"1.00"]` answered the terse sentence, so the quality of the
    ' diagnostic depended on which kind happened to come first.
    check("and names the element     ", contains(error.message, "element 1 is unknown"), true)
    error.clear()
    s3 = sum([7, {USD}"1.00"])
    check("either order names it    ", contains(error.message, "element 1 is money"), true)
    ' AND `money` TAKES NO ARTICLE: it is a mass noun, as are `nothing` and
    ' `unknown`, which is why naming a kind in a sentence is one helper rather
    ' than an article glued onto every call site.
    check("money is not `a money`   ", contains(error.message, "a money"), false)
    error.clear()
    ' MIXED CURRENCIES ARE A MISSING RATE, NOT AN ARITHMETIC QUESTION, and each
    ' aggregate says what the OPERATOR says for the same pair -- `+` for the
    ' fold, `<` for the ordering -- so the two cannot drift apart.
    cross = [ {USD}"1.00", {EUR}"2.00" ]
    cs = sum(cross)
    check("mixed currency: add      ", contains(error.message, "cannot add money in different currencies (USD and EUR)"), true)
    error.clear()
    cm = max(cross)
    check("mixed currency: order    ", contains(error.message, "cannot order money in different currencies (USD and EUR)"), true)
    error.clear()
    ' A MONTH HAS NO FIXED LENGTH, which is exactly why `1 month < 2 days`
    ' refuses. The comparator cannot raise, so it is refused in a pre-pass.
    months = [ 1 month, 2 days ]
    ms = sort(months)
    check("month duration refused   ", contains(error.message, "a month has no fixed length"), true)
    error.clear()
    ' AND A MONEY ARRAY WITH A NUMBER IN IT names the element, like projection.
    ragged = [ {USD}"1.00", 7 ]
    rs = sum(ragged)
    check("money with a number      ", contains(error.message, "element 1 is a number"), true)
    error.clear()
    ' THE SIX THAT STAY NUMERIC, stated rather than discovered: a variance of
    ' money is money squared, and the other five are not among the aggregates
    ' the loop survey measured.
    md = median(inv.total)
    check("median stays numeric     ", contains(error.message, "numeric array"), true)

    print "-- CONTROLS: nothing that worked before moved"
    error.clear()
    a = [3, 1, 2]
    sort(a)
    check("sort still mutates       ", a, [1, 2, 3])
    r2 = [1, 2, 3]
    reverse(r2)
    check("reverse still mutates    ", r2, [3, 2, 1])
    check("sort of a projection     ", sort(rows.item), ["hammer", "nails", "saw"])
    check("a record field is a field", rows[0].item, "hammer")
    check("max of numbers           ", max([3, 9, 1]), 9)
end program
