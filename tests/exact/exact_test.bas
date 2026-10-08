' EXACT NUMBERS -- exactness as a property of a `number`, not a second type.
'
' THE DEFECT: `number` is the only numeric kind and it is a double, so integer
' arithmetic went silently wrong above 2^53. That is the silent-wrong-answer class
' this tree organises against everywhere else -- and it was INTERNALLY
' inconsistent, because `run_odbc.sh`'s exactness tier inserts 2^53+1, reads it
' back as a STRING, and asserts both that the digits survived AND that a double
' would have changed them. So gBASIC refused to lose precision across a driver and
' lost it in its own arithmetic. One of those two positions was wrong.
'
' ONE NUMERIC KIND. A `number` is EXACT when it is an integer the runtime holds
' exactly, INEXACT otherwise -- Scheme's model and Lua 5.3's, not an invention.
' `type()` still answers `number`: no 28th kind, no promotion matrix to get wrong.
'
' SELF-CHECKING, NOT GOLDEN, AND FORCED: the defect produces a PLAUSIBLE NUMBER
' one digit out, and a golden would record it as expected -- which is exactly how
' it survived until now.

function check(label, got, want)
    if string(got) = string(want) then
        print "ok   " + label
    else
        print "MISMATCH " + label + ": got " + string(got) + ", want " + string(want)
    end if
    return nothing
end function

program main( args )
    print "-- an integer literal is exact, and its arithmetic stays exact"
    check("the headline sum         ", 9007199254740992 + 1, 9007199254740993)
    check("and it is not 2^53       ", (9007199254740992 + 1) = 9007199254740992, false)
    check("difference               ", 9007199254740993 - 1, 9007199254740992)
    check("product                  ", 4503599627370496 * 2, 9007199254740992)
    check("a product past 2^53      ", 3000000000 * 3000000000, 9000000000000000000)
    check("and it survives a name   ", 9007199254740993 - 1 = 9007199254740992, true)

    print "-- COMPARISON, where a mistake would be invisible"
    ' THE RULE THAT MATTERS, and the obvious implementation gets it wrong:
    ' `(double)exact == d` rounds the exact side first, so this answers TRUE. The
    ' same mistake was already made once in this tree, in a precision-loss
    ' predicate that reported zero lossy operations for exactly this reason.
    check("exact vs inexact double  ", 9007199254740993 = 9007199254740992.0, false)
    check("the other way round      ", 9007199254740992.0 = 9007199254740993, false)
    check("ordering follows          ", 9007199254740993 > 9007199254740992.0, true)
    check("and its converse          ", 9007199254740992.0 < 9007199254740993, true)
    check("exact vs exact            ", 9007199254740993 = 9007199254740993, true)
    ' A FRACTION BREAKS A TIE on equal whole parts, in both signs.
    check("3 < 3.5                  ", 3 < 3.5, true)
    check("3 > 2.5                  ", 3 > 2.5, true)
    check("-3 > -3.5                ", -3 > -3.5, true)
    check("-3 < -2.5                ", -3 < -2.5, true)
    check("4 = 4.0                  ", 4 = 4.0, true)

    print "-- what is NOT exact, and each is a decision rather than an omission"
    ' A FRACTION, obviously. An EXPONENT literal, because the notation says
    ' "approximately this magnitude" -- `number("...")` is how to ask for the
    ' integer, and this was an open decision in the design settled here.
    check("a fraction               ", 0.5 + 0.25, 0.75)
    check("floating addition unmoved", 0.1 + 0.2, 0.30000000000000004)
    check("an exponent literal      ", 1e16 + 1 = 10000000000000001, false)
    ' MIXED IS INEXACT: `exact + inexact` has no exact answer, and promoting the
    ' inexact side would invent precision it never had. That is also why there is
    ' no promotion matrix -- one numeric kind, one rule.
    check("mixed falls back         ", 9007199254740992 + 1.0 = 9007199254740993, false)
    ' DIVISION IS DELIBERATELY ABSENT: integer division is not integer-valued, and
    ' a rational kind is a different language.
    check("division is inexact      ", 6 / 3, 2)
    check("and 1/3 unchanged        ", round(1 / 3, 6), 0.333333)
    ' `pow` IS INEXACT UNTIL ITS OWN INCREMENT, stated so the gap is a recorded
    ' decision rather than a surprise: the design's own headline example,
    ' `pow(2, 53) + 1`, is NOT fixed by this increment.
    check("pow is still inexact     ", pow(2, 53) + 1 = 9007199254740993, false)

    print "-- NEGATION preserves exactness, in sign as well as magnitude"
    ' It did not, at first: `-9007199254740993` printed ...992 while its positive
    ' counterpart printed ...993, because unary minus went through the double.
    check("unary minus              ", -9007199254740993, -9007199254740993)
    check("and compares exactly     ", -9007199254740993 = -9007199254740992.0, false)
    check("subtraction from zero    ", 0 - 9007199254740993, -9007199254740993)
    check("double negation          ", -(-9007199254740993), 9007199254740993)

    print "-- RENDERING shows the digits the value actually holds"
    ' Folded in rather than deferred: without it `print x` shows the double view
    ' while `x = 9007199254740993` answers true, so a program can PROVE a value the
    ' display contradicts -- a new silent-wrong-answer shape, worse than the loss.
    big = 9007199254740993
    check("print                    ", string(big), "9007199254740993")
    check("inside an array          ", string([big]), "[9007199254740993]")
    check("inside a record          ", string({ n: big }), "{\"n\":9007199254740993}")
    check("a small integer unchanged", string(42), "42")
    check("a fraction unchanged     ", string(0.5), "0.5")
    check("and `type` still answers ", type(big), "number")

    print "-- OVERFLOW DEGRADES rather than refusing, and says so"
    ' Refusing would stop a program that answers today, and "nothing that ran stops
    ' running" is the rule. The answer is the one the program used to get; what is
    ' new is that the loss is loud.
    on warning goto next
    s1 = 9000000000000000000 + 9000000000000000000
    check("a sum that overflows     ", warning.code, 2112)
    check("and it still answers     ", s1 > 1e18, true)
    on warning goto next
    p1 = 4000000000000 * 4000000000000
    check("a product that overflows ", contains(warning.message, "product"), true)
    on warning goto next
    d1 = 9000000000000000000 - (0 - 9000000000000000000)
    check("a difference that does   ", contains(warning.message, "difference"), true)
    ' AND THE CONTROL: arithmetic that fits must be SILENT, or "it warns on
    ' overflow" is satisfied by warning on every sum.
    drained = warning
    on warning goto next
    ok1 = 9007199254740992 + 1
    check("no overflow, no warning  ", warning = false, true)
    drained = warning
    on warning goto next
    ok2 = 2 + 2
    check("nor on small arithmetic  ", warning = false, true)

    print "-- the coercion PLAT-EQ measured at 1,472 uses is untouched"
    ' A boolean is never exact, so `0 = false` keeps the double path.
    check("0 = false                ", 0 = false, true)
    check("1 = true                 ", 1 = true, true)
    check("0 != true                ", 0 = true, false)

    print "-- CONTROLS: ordinary arithmetic is where it was"
    check("2 + 2                    ", 2 + 2, 4)
    check("2 * 3                    ", 2 * 3, 6)
    check("7 - 9                    ", 7 - 9, -2)
    check("mod                      ", mod(-7, 3), 2)
    check("a hex literal            ", 0xFF, 255)
    check("hex arithmetic           ", 0xFF + 1, 256)
    check("a duration still parses  ", string(2 days), "2 days")
    check("money still parses       ", string({USD}"19.95"), "19.95")
end program
