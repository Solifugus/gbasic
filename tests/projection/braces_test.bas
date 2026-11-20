' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' Braces on plain data (tests/run_projection.sh, tier braces): G-SQL's
' `{ fields }` on gBASIC's own records and arrays. THE ORACLE IS THE LOOP IT
' REPLACES: each answer is also built by hand with `for each`, and the two
' must be equal. Then what a loop cannot argue with: the order of the fields,
' a missing field, a step through an array, the refusals.

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
    rows = [ { item: "saw", name: "Saw", customer: { name: "Ann" } } ]
    r = { a: 1 }
    if what = "a name twice" then
        x = r{ a, a }
    end if
    if what = "two levels, one name" then
        x = rows{ name }.customer{ name }
    end if
    if what = "a missing field of one record" then
        x = r{ a, b }
    end if
    if what = "a row that is not a record" then
        x = [ { a: 1 }, 7 ]{ a }
    end if
    if what = "the neighbour" then
        x = rows{ item }.customer{ name }
    end if
    return "ACCEPTED"
handle:
    m = error.message
    error.clear()
    return m
end function

program main(args)
    rows = [ { item: "hammer", amount: 19.95, paid: true,
               customer: { name: "Ann", city: "Rome" }, lines: [ { sku: "h1" }, { sku: "h2" } ] },
             { item: "saw", amount: 32.50, paid: false,
               customer: { name: "Bo", city: "Oslo" }, lines: [] },
             { item: "nails", amount: 7.00, paid: true,
               customer: { name: "Cy", city: "Lima" }, lines: [ { sku: "n1" } ] } ]

    print "-- ONE RECORD: the fields asked for, in the order asked"
    r = rows[0]
    check("r{ amount, item }", r{ amount, item }, { amount: 19.95, item: "hammer" })
    check("  a new value: r is untouched", count(keys(r)), 5)

    print "-- AN ARRAY: a record per row; the loop it replaces agrees"
    want = []
    for each x in rows
        want = append(want, { item: x.item, amount: x.amount })
    next
    check("rows{ item, amount }", rows{ item, amount }, want)
    check("  as many rows as there were", count(rows{ item }), count(rows))

    print "-- A STEP, then braces: flat, path order then brace order"
    want = []
    for each x in rows
        want = append(want, { item: x.item, name: x.customer.name, city: x.customer.city })
    next
    check("rows{ item }.customer{ name, city }", rows{ item }.customer{ name, city }, want)
    want = []
    for each x in rows
        want = append(want, { name: x.customer.name })
    next
    check("braces only at the end: rows.customer{ name }", rows.customer{ name }, want)

    print "-- A STEP INTO AN ARRAY: a row per element, as a join"
    want = []
    for each x in rows
        for each l in x.lines
            want = append(want, { item: x.item, sku: l.sku })
        next
    next
    check("rows{ item }.lines{ sku }", rows{ item }.lines{ sku }, want)
    check("  the saw has no lines and gives no row", count(rows{ item }.lines{ sku }), 3)
    check("a trailing step with no braces keeps a row per element",
          rows{ item }.lines, [ { item: "hammer" }, { item: "hammer" }, { item: "nails" } ])
    check("one record stepping into an array is many", r{ item }.lines{ sku },
          [ { item: "hammer", sku: "h1" }, { item: "hammer", sku: "h2" } ])

    print "-- A MISSING FIELD: unknown in a row of many, kept"
    mixed = [ { a: 1, b: 2 }, { a: 3 } ]
    got = mixed{ a, b }
    check("  the row is kept", count(got), 2)
    check("  and its b is unknown", is_unknown(got[1].b), true)
    check("  beside one that has it", got[0].b, 2)

    print "-- THE ROOT IS EVALUATED ONCE"
    ' a function cannot change its caller's variables, so it marks a file
    mark = temp_file()
    got = made(mark){ x }
    check("a call as the root ran once", len(read({file}mark)), 1)
    check("  and gave its answer", got, { x: 1 })
    delete({file}mark)

    print "-- WHAT IT IS NOT: a projection still reads one field"
    check("rows.item is still the array of items", rows.item, [ "hammer", "saw", "nails" ])
    check("(r{ item }).item reads through the braces", (r{ item }).item, "hammer")
    check("rows{ item }[1] indexes the answer", rows{ item }[1], { item: "saw" })
    check("an empty array gives an empty one", []{ a }, [])

    print "-- REFUSALS, each beside its legal neighbour"
    check("a name twice", refusal("a name twice"), "`a` is named twice in one pair of braces")
    check("two levels, one name", refusal("two levels, one name"),
          "`rows` and `rows.customer` both give `name` -- leave one out")
    check("a missing field of one record raises", refusal("a missing field of one record"),
          "unknown record field: b")
    check("a row that is not a record", refusal("a row that is not a record"),
          "`a` expects every row to be a record; row 1 is a number")
    check("the neighbour is accepted", refusal("the neighbour"), "ACCEPTED")
end program

function made(mark)
    write({file}mark, read({file}mark) + "x")
    return { x: 1, y: 2 }
end function
