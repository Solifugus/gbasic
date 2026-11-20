' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' Brackets on plain data (tests/run_projection.sh, tier brackets): G-SQL's
' `[.field = value]` filter on gBASIC's own arrays. THE ORACLE IS THE LOOP IT
' REPLACES: each answer is also built with `for each` and `if`, and the two
' must be equal. Then what a loop cannot argue with: unknown dropping a row,
' commas as `and`, a filter inside a filter, a filter in a brace chain, and
' the refusals beside their neighbour.

function check(label, got, want)
    if got = want and string(got) = string(want) then
        print "ok   " + label
    else
        print "MISMATCH " + label + ": got " + string(got) + ", want " + string(want)
    end if
    return nothing
end function

function refusal(what)
    on error goto handle
    rows = [ { a: 1 }, { a: 2 } ]
    r = { a: 1 }
    if what = "a condition that is not true or false" then
        x = rows[.a]
    end if
    if what = "a filter on one record" then
        x = r[.a > 0]
    end if
    if what = "a row that is not a record" then
        x = [ { a: 1 }, 7 ][.a > 0]
    end if
    if what = "a list with no condition" then
        x = rows[0, 1]
    end if
    if what = "the neighbour" then
        x = rows[.a > 1]
    end if
    return "ACCEPTED"
handle:
    m = error.message
    error.clear()
    return m
end function

program main(args)
    rows = [ { item: "hammer", amount: 19.95, paid: true,
               customer: { name: "Ann", city: "Rome" }, lines: [ { sku: "h1", qty: 2 }, { sku: "h2", qty: 5 } ] },
             { item: "saw", amount: 32.50, paid: false,
               customer: { name: "Bo", city: "Oslo" }, lines: [] },
             { item: "nails", amount: 7.00,
               customer: { name: "Cy", city: "Rome" }, lines: [ { sku: "n1", qty: 9 } ] } ]

    print "-- A CONDITION: the rows themselves, those it holds for"
    want = []
    for each r in rows
        if r.amount > 10 then
            want = append(want, r)
        end if
    next
    check("rows[.amount > 10]", rows[.amount > 10], want)
    check("  the rows are whole, not chosen fields", count(keys(rows[.amount > 10][0])), 5)

    print "-- UNKNOWN DROPS A ROW, as SQL's WHERE: nails has no paid"
    want = []
    for each r in rows
        if not is_unknown(r["paid"]) and r["paid"] = true then
            want = append(want, r)
        end if
    next
    check("rows[.paid]", rows[.paid], want)
    check("  and not .paid drops it too", rows[not .paid].item, [ "saw" ])

    print "-- COMMAS ARE AND; a program's own values and nested fields"
    city = "Rome"
    want = []
    for each r in rows
        if r.amount > 5 and r.customer.city = city then
            want = append(want, r)
        end if
    next
    check("rows[.amount > 5, .customer.city = city]", rows[.amount > 5, .customer.city = city], want)
    check("  the same as `and`", rows[.amount > 5 and .customer.city = city], want)

    print "-- A FILTER INSIDE A CONDITION filters that row's own array"
    want = []
    for each r in rows
        n = 0
        for each l in r.lines
            if l.qty > 4 then
                n = n + 1
            end if
        next
        if n > 0 then
            want = append(want, r)
        end if
    next
    check("rows[count(.lines[.qty > 4]) > 0]", rows[count(.lines[.qty > 4]) > 0], want)
    ' AND THE OUTER ROW AFTER IT: a field read once the inner filter is done
    ' must be the outer row's, so the rows are a stack, not one slot
    check("  and the outer row's field read after it",
          rows[count(.lines[.qty > 4]) > 0 and .amount > 10].item, [ "hammer" ])

    print "-- IN A BRACE CHAIN: a step of the chain"
    want = []
    for each r in rows
        if not is_unknown(r["paid"]) and r["paid"] = true then
            want = append(want, { item: r.item, amount: r.amount })
        end if
    next
    check("rows[.paid]{ item, amount }", rows[.paid]{ item, amount }, want)
    want = []
    for each r in rows
        for each l in r.lines
            if l.qty > 3 then
                want = append(want, { item: r.item, sku: l.sku })
            end if
        next
    next
    check("rows{ item }.lines[.qty > 3]{ sku }", rows{ item }.lines[.qty > 3]{ sku }, want)
    check("a filter after a step to one record keeps or drops the row",
          rows{ item }.customer[.city = "Rome"]{ name },
          [ { item: "hammer", name: "Ann" }, { item: "nails", name: "Cy" } ])

    print "-- WHAT IT IS NOT: an index is still an index"
    check("rows[1] is the second row", rows[1].item, "saw")
    i = 2
    check("rows[i] reads a variable, not a field", rows[i].item, "nails")
    check("a filter keeping nothing is empty", rows[.amount > 1000], [])
    check("projection over a filter", rows[.amount < 30].customer.name, [ "Ann", "Cy" ])

    print "-- REFUSALS, each beside its legal neighbour"
    check("a condition that is not true or false", refusal("a condition that is not true or false"),
          "a condition in brackets is true or false, and this one is a number -- compare it (`[.amount > 0]`)")
    check("a filter on one record", refusal("a filter on one record"),
          "a condition in brackets chooses rows of an array (`rows[.paid]`), and this is a record")
    check("a row that is not a record", refusal("a row that is not a record"),
          "a condition in brackets expects every row to be a record; row 1 is a number")
    check("a list with no condition is still refused", contains(refusal("a list with no condition"),
          "an array or a record takes one index"), true)
    check("the neighbour is accepted", refusal("the neighbour"), "ACCEPTED")
end program
