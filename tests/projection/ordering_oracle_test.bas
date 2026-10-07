' THE ORDERING OPERATOR IS THE ORACLE FOR THE SORTER.
'
' `sort`, `min` and `max` ask one question -- what is smaller -- and so does
' `<`. Two implementations of one question drift, and they HAD: `a.total <
' b.total` answered for money and `sort(invoices.total)` refused, while
' `1 month < 2 days` refused and nothing stopped a comparator from answering it.
' The rule asserted here is that the two AGREE, with `<` as the authority,
' because `<` is where the language already wrote down what can be ordered.
'
' WHY THIS IS AN ORACLE AND NOT A SECOND CALL INTO THE SAME CODE: `<` is
' `eval_comparison`, a value-returning branch chain that can raise; `sort` is
' `qsort` over `sort_value_compare`, which returns an int and CANNOT raise, so
' its conditional refusals live in a separate pre-pass. Different code, same
' question -- which is what makes agreement evidence.
'
' ONE DOCUMENTED EXCEPTION, asserted as an exception rather than left to be
' discovered: an ABSENCE. `<` refuses `unknown < 1` because there is no answer;
' `sort` must still PUT IT SOMEWHERE, and ranks it below every ordinary value.
' That divergence is deliberate and is pinned here so it cannot spread.

function fails(label, got, want)
    if got = want then
        print "ok   " + label
    else
        print "MISMATCH " + label + ": got " + string(got) + ", want " + string(want)
    end if
    return nothing
end function

' Does `<` answer for this pair?
function operator_answers(a, b)
    on error goto next
    x = a < b
    if error then
        return false
    end if
    return true
end function

' Does `sort` answer for this pair?
function sorter_answers(a, b)
    on error goto next
    s = sort([a, b])
    if error then
        return false
    end if
    return true
end function

' Both must give the same verdict, and where both answer they must agree about
' WHICH IS SMALLER -- a comparator that accepts everything and orders it wrongly
' passes an agreement check that only compares the two verdicts.
function agree(label, a, b)
    op = operator_answers(a, b)
    so = sorter_answers(a, b)
    if op != so then
        print "MISMATCH " + label + ": `<` says " + string(op) + ", sort says " + string(so)
        return nothing
    end if
    if op = false then
        print "ok   " + label + " (both refuse)"
        return nothing
    end if
    s = sort([a, b])
    smaller_first = a < b
    if smaller_first then
        same = string(s[0]) = string(a)
    else
        same = string(s[0]) = string(b)
    end if
    if same then
        print "ok   " + label + " (both answer, same order)"
    else
        print "MISMATCH " + label + ": sort put " + string(s[0]) + " first, `<` disagrees"
    end if
    return nothing
end function

program main( args )
    print "-- the kinds that were always ordered"
    agree("number      ", 1, 2)
    agree("string      ", "a", "b")
    agree("boolean     ", false, true)
    agree("datetime    ", {date}"2025-01-01", {date}"2026-03-07")

    print "-- the two this increment brought into agreement"
    agree("money       ", {USD}"1.00", {USD}"2.00")
    agree("duration    ", 1 hour, 3 days)

    print "-- the conditional refusals, which must match `<` exactly"
    agree("money x-ccy ", {USD}"1.00", {EUR}"2.00")
    agree("month span  ", 1 month, 2 days)

    print "-- the kinds neither orders"
    agree("record      ", { a: 1 }, { a: 2 })
    agree("array       ", [1], [2])
    agree("mixed kinds ", 1, "a")

    print "-- THE ONE DELIBERATE DIVERGENCE: an absence"
    ' `<` refuses because there is no answer; `sort` must place it, and ranks it
    ' below every ordinary value. If this ever starts agreeing, either `<` has
    ' begun inventing an order or `sort` has begun refusing arrays with holes --
    ' and the second would break `sort([1, unknown, 3])`, which works today.
    fails("`<` refuses an absence   ", operator_answers(unknown, 1), false)
    fails("sort places an absence   ", sorter_answers(unknown, 1), true)
    fails("and ranks it first       ", string(sort([3, unknown, 1])), "[unknown,1,3]")

    print "-- min/max take the same verdict as sort, being the same gate"
    on error goto next
    error.clear()
    x = max([{USD}"1.00", {EUR}"2.00"])
    fails("max refuses x-currency   ", contains(error.message, "different currencies"), true)
    error.clear()
    y = min([1 month, 2 days])
    fails("min refuses a month      ", contains(error.message, "no fixed length"), true)
    error.clear()
    ' CONTROL: and answers for everything sort answers for, or the agreement
    ' above would be satisfied by min/max refusing the newly-ordered kinds.
    fails("max answers money        ", string(max([{USD}"1.00", {USD}"2.00"])), "2.00")
    fails("min answers a duration   ", string(min([3 days, 1 hour])), "1 hour")
end program
