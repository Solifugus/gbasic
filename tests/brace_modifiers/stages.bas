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

' A stage that RECORDS when it ran, so the ORDER is observable. The order is a
' real choice and was not pinned until it was asked about.
modifier mark_a(  ) for assign
    append(trace, "A:" + string(value))
    return value + "a"
end modifier

modifier mark_b(  ) for assign
    append(trace, "B:" + string(value))
    return value + "b"
end modifier

' A stage that is NOT a pure function of its input: it answers something
' different every time it is called.
modifier ticking(  ) for assign
    append(ticks, "t")
    return string(count(ticks))
end modifier

function check(label, got, want)
    if string(got) = string(want) then
        print "ok   " + label
    else
        print "MISMATCH " + label + ": got " + string(got) + ", want " + string(want)
    end if
    return nothing
end function

program main( args )
    ' The modifiers above append to these, and a top-level assignment would NOT
    ' run (warning 2106) -- the block is what executes. They are set here,
    ' before the first clause that uses one.
    trace = []
    ticks = []
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
        check("and WHICH stage it is", contains(error.message, "stage 1"), true)
        error.clear()
    else
        check("unknown stage named  ", "accepted", "refused")
    end if
    ' THE ASSIGN SIDE NAMES THE STAGE TOO, which is what the gbasic-books
    ' session asked for before this shipped: `{trimmed; nosuch; upper}` and
    ' `{trimmed; upper; nosuch}` gave the IDENTICAL message, and "which part was
    ' not found" is the question a reader has.
    w {trimmed; nosuch; upper}= s
    if error then
        check("assign: stage named  ", contains(error.message, "stage 2"), true)
        error.clear()
    else
        check("assign: stage named  ", "accepted", "refused")
    end if
    w2 {trimmed; upper; nosuch}= s
    if error then
        check("and a different stage", contains(error.message, "stage 3"), true)
        error.clear()
    else
        check("and a different stage", "accepted", "refused")
    end if
    ' CONTROL: a ONE-stage clause keeps its sentence exactly, including the
    ' redirect that is the commonest cause of it -- a stage number on a clause
    ' with one stage would be noise, and this is what stops the suffix leaking.
    w3 {nosuch}= s
    if error then
        check("one stage: no number ", contains(error.message, "stage"), false)
        error.clear()
    end if
    w4 {caseless}= s
    if error then
        check("the lens redirect too", contains(error.message, "comparison lens"), true)
        check("and no stage number  ", contains(error.message, "stage"), false)
        error.clear()
    end if
    ' A STAGE THAT IS FOUND AND THEN FAILS reports its own cause WITHOUT a stage
    ' number -- a known and pinned limit, not an oversight: amending a pending
    ' error means touching the path 333 negative goldens rest on. The modifier's
    ' name identifies the stage in every chain that does not repeat one.
    w5 {number; trimmed}= "  42  "
    if error then
        check("a type failure names  ", contains(error.message, "trim expects"), true)
        check("COST: no stage number ", contains(error.message, "stage"), false)
        error.clear()
    end if
    ' CONTROL: a one-stage clause is untouched in both contexts, or the whole
    ' change is satisfied by a build that refuses every clause.
    z {trimmed}= s
    check("CONTROL one stage    ", z, "hello")
    check("CONTROL one lens     ", "a" {caseless}= "A", true)

    print ""
    print "-- WHAT ORDER, exactly: the sequencing is STAGE-MAJOR on a comparison"
    ' ASKED 2026-10-03 and pinned because the answer is a CHOICE. On the assign
    ' side there is only one order. On a comparison each stage is applied to
    ' BOTH operands, and there are two ways to sequence that:
    '
    '   stage-major   A(left) A(right) B(left) B(right)   <- what this does
    '   operand-major A(left) B(left)  A(right) B(right)
    '
    ' They give the SAME ANSWER for a pure stage and differ only in the order of
    ' side effects -- so neither is observable from a correct program, which is
    ' exactly why it needed pinning rather than being left to the code.
    '
    ' STAGE-MAJOR IS DELIBERATE, for ADJACENCY: the two calls to one stage are
    ' back to back, so a stage that reads anything outside its argument -- a
    ' clock, a counter, a file -- sees the two operands at as nearly the same
    ' moment as possible. Operand-major separates them by the whole rest of the
    ' chain.
    trace = []
    probe_assign {mark_a; mark_b}= "_"
    check("assign: A then B     ", join(trace, " "), "A:_ B:_a")
    check("and B saw A's output ", probe_assign, "_ab")
    trace = []
    probe_cmp = ("L" {mark_a; mark_b}= "R")
    check("compare: stage-major ", join(trace, " "), "A:L A:R B:La B:Ra")

    print ""
    print "-- THE COST: a comparison runs each stage TWICE"
    ' Once per operand, necessarily -- you cannot normalise both sides without
    ' calling the normaliser on both sides. Worth knowing because a lens LOOKS
    ' like a pure test, and a three-stage chain is six invocations of user code.
    trace = []
    probe_n = ("L" {mark_a; mark_b}= "R")
    check("2 stages -> 4 calls  ", count(trace), 4)
    trace = []
    probe_m {mark_a; mark_b}= "_"
    check("and assign -> 2      ", count(trace), 2)

    print ""
    print "-- AND SO A STAGE MUST BE A PURE FUNCTION OF ITS INPUT"
    ' This is inherent rather than a defect: a stage that answers differently
    ' each call gets called twice with the two operands, so EQUAL VALUES COMPARE
    ' UNEQUAL. Nothing can detect it, so it is documented and pinned here --
    ' the demonstration IS the warning.
    ticks = []
    check("non-pure breaks it   ", "same" {ticking}= "same", false)
    check("because it ran twice ", count(ticks), 2)
    ' CONTROL: the same comparison through a PURE stage is true, or the check
    ' above would be satisfied by a lens that reports false for everything.
    check("CONTROL pure is true ", "same" {trimmed}= "same", true)
end program
