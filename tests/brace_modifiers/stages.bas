' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' SEVERAL MODIFIERS IN ONE CLAUSE, separated by a top-level `;`
' (docs/brace_modifier_design.md §9).
'
' WHY `;` AND NOT `,`: the comma is already the ARGUMENT separator --
' `{between "a", "b"}` works and `{between "a" "b"}` is refused -- and arity
' cannot disambiguate the two, because optional arguments exist, so
' `{split ",", trimmed}` is a second argument or a second stage with nothing to
' choose between them. `;` is unclaimed: it is not a token in gBASIC at all, so
' `x = 1; y = 2` is a LEXER error. Measured, both ways, before choosing.
'
' WHY THE COMPARISON HALF IS THE VALUABLE ONE. `caseless` was the only compare
' lens, so an assign modifier could not be used to compare at all: `a {trimmed}= b`
' reported `compare modifier not found: trimmed`. And `{caseless}` alone answers
' FALSE for `"  Joe  "` against `"joe"`, because the spaces defeat it. The whole
' job had to be written `trim(lower(a)) = trim(lower(b))` -- converting both
' sides in both ways, which is the work this removes.
'
' A COMPARISON LENS IS A NORMALISATION OF BOTH SIDES, which is what makes it
' compose, and the pattern is not new: the datetime precision lenses already
' lens both operands and re-enter the comparison with the modifier cleared.
'
' SELF-CHECKING, not a transcript: every defect here is a PLAUSIBLE ANSWER --
' a stage silently skipped yields an ordinary string, and a comparison with one
' stage dropped yields an ordinary `false`. A golden would record either.

function check(label, got, want)
    if string(got) = string(want) then
        print "ok   " + label
    else
        print "MISMATCH " + label + ": got " + string(got) + ", want " + string(want)
    end if
    return nothing
end function

program main( args )
    print "-- assign: stages run LEFT TO RIGHT"
    s = "  hello  "
    a {trimmed; upper}= s
    check("trimmed then upper   ", a, "HELLO")

    ' ORDER IS ASSERTED WITH A NON-COMMUTING PAIR, because `trimmed` and `upper`
    ' COMMUTE on this input and agree either way -- a pair that agrees proves
    ' nothing about order.
    n = "  42  "
    b {trimmed; number}= n
    check("trimmed then number  ", b, 42)
    check("and it IS a number   ", type(b), "number")

    print ""
    print "-- three stages, and a ; INSIDE an argument is not a separator"
    t = "  a,b,c  "
    c {trimmed; split ","; join "-"}= t
    check("three stages         ", c, "a-b-c")
    parts = ["a", "b", "c"]
    d {join "; "}= parts
    check("; inside an argument ", d, "a; b; c")
    e {join "; "; upper}= parts
    check("both in one clause   ", e, "A; B; C")

    print ""
    print "-- compare: the case this exists for"
    x = "  Joe  "
    y = "joe"
    check("caseless alone       ", x {caseless}= y, false)
    check("trimmed alone        ", x {trimmed}= y, false)
    check("trimmed THEN caseless", x {trimmed; caseless}= y, true)
    ' The CONTROL that says the clause is doing the work rather than the
    ' comparison having become lax: two values that differ in CONTENT must
    ' still be unequal through the same clause.
    check("and it is not lax    ", x {trimmed; caseless}= "jane", false)

    print ""
    print "-- compare: a single assign modifier normalises both sides"
    ' This did not work AT ALL before: `trimmed` was not a compare lens.
    check("{upper} both sides   ", "joe" {upper}= "JOE", true)
    check("{trimmed} ordering   ", "  b  " {trimmed}> "a", true)
    check("{number} both sides  ", " 7 " {number}= "7", true)
    ' BOTH SIDES, AND THE RIGHT ONE NEEDS ITS OWN CHECK. Every case above is
    ' also satisfied by normalising only the LEFT operand -- measured: with the
    ' right-hand call deleted, `{trimmed; caseless}` still answers true, because
    ' `caseless` absorbs the difference that is left. So the discriminating case
    ' is one where ONLY THE RIGHT side needs the stage.
    check("the RIGHT side too   ", "b" {trimmed}= "  b  ", true)
    check("and both at once     ", "  b  " {trimmed}= "  b  ", true)

    print ""
    print "-- the datetime lenses are untouched"
    check("{day}                ", ({date}"2026-01-05") {day}= ({datetime}"2026-01-05 13:00:00"), true)
    check("{month}              ", ({date}"2026-01-05") {month}= ({date}"2026-01-20"), true)

    print ""
    print "-- refusals, each beside a legal neighbour"
    on error goto next
    r = ("a" {caseless; trimmed}= "A")
    if error then
        check("terminal must be last", contains(error.message, "must be the last stage"), true)
        error.clear()
    else
        check("terminal must be last", "accepted", "refused")
    end if
    q = ("a" {nosuch; trimmed}= "a")
    if error then
        check("unknown stage named  ", contains(error.message, "nosuch"), true)
        error.clear()
    else
        check("unknown stage named  ", "accepted", "refused")
    end if
    ' CONTROL: a one-stage clause is untouched in both contexts, or the whole
    ' change is satisfied by a build that refuses every clause.
    z {trimmed}= s
    check("CONTROL one stage    ", z, "hello")
    check("CONTROL one lens     ", "a" {caseless}= "A", true)
end program
