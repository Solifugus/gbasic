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
    ' `pow` WAS THE RECORDED GAP AND IT IS CLOSED (increment 5, 2026-10-13). This
    ' line asserted the LIMITATION -- "pow is still inexact" -- and went red the
    ' moment it stopped being true, naming itself as the thing to update. That is
    ' the negative-control pattern working, and the replacement is the opposite
    ' claim: the design's own HEADLINE EXAMPLE, the first line of
    ' exact_number_design.md, now answers.
    check("THE headline example     ", pow(2, 53) + 1, 9007199254740993)

    print "-- `number(\"...\")` IS A SOURCE OF EXACTNESS, which it was not until now"
    ' THE DESIGN NAMED THIS REMEDY AND NOTHING IMPLEMENTED IT. "Where exactness
    ' comes from" lists `number("...")` on integer text, and the settled ruling on
    ' `1e20` points at it in as many words -- the comment thirty lines above says
    ' so. Measured 2026-10-12: only LITERALS shipped, so the remedy a settled
    ' decision names handed back an inexact number and
    ' `number("9007199254740992") + 1` answered ...992. The fixture stated the
    ' remedy in a COMMENT and never asserted it, which is how it survived.
    '
    ' ASSERTED AS ARITHMETIC, NOT AS A VALUE, and that is load-bearing: below
    ' 2^53 an exact and an inexact integer render identically through `string`
    ' and `encode` (measured), so `string(number("5"))` passes on both builds.
    ' Only carrying the value through `+ 1` can tell them apart.
    check("integer text is exact    ", number("9007199254740992") + 1, 9007199254740993)
    check("and compares exactly     ", number("9007199254740993") = 9007199254740992.0, false)
    check("negative integer text    ", number("-9007199254740993"), -9007199254740993)
    ' BOTH DOORS, because there are two independent string-to-number paths -- the
    ' call and the `{number}` modifier -- and they were two separate copies of
    ' `strtod`. A fix reaching one is the read-and-write-halves-disagree shape.
    from_modifier = {number}"9007199254740992"
    check("the modifier agrees      ", from_modifier + 1, 9007199254740993)
    ' THE CONTROLS, without which "integer text is exact" is satisfied by a
    ' conversion that calls everything exact. Each is a settled decision:
    ' exponent notation means "approximately this magnitude" WHICHEVER DOOR it
    ' comes through, so text is judged the same way a literal is.
    check("exponent text inexact    ", number("1e16") + 1 = 10000000000000001, false)
    check("fraction text inexact    ", number("0.5") + 0.25, 0.75)
    check("hex text inexact         ", number("0x10") + 1, 17)
    ' OUT OF int64 RANGE DEGRADES rather than raising, as the literal rule has it.
    check("past int64 degrades      ", number("99999999999999999999") > 1e19, true)

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

    print "-- INCREMENT 5: exactness past the operators"
    ' ONE RULE, AND IT PREDICTS EVERY LINE BELOW: a function that **picks** a
    ' value preserves exactness; one that **calculates** is exact when both
    ' operands are exact and the answer is an integer the runtime holds. An
    ' inexact operand never yields an exact answer, which is not new policy --
    ' it is what `+` `-` `*` already do.
    '
    ' MEASURED BEFORE THE CHANGE, every one of these answering ...92 with exit 0
    ' and no diagnostic, and `mod(b, 2)` answering **0** -- not lost precision
    ' but a WRONG ANSWER, since the exact odd literal had become an even double
    ' before `mod` saw it. A parity test is the commonest thing anyone asks
    ' `mod`, and it reported the opposite of the truth.
    b = 9007199254740993

    print "-- a selector PICKS, so it returns the author's own value"
    ' THE ARGUMENT THAT THE OLD CODE WAS A DEFECT AND NOT A DECISION IS THE
    ' NEIGHBOURING INCONSISTENCY: `min`/`max` over text already returned the
    ' element, and only the numeric path recomputed a double. So `max(["b","a"])`
    ' gave back what the author wrote and `max([b, b])` gave a rounded view of it.
    check("min picks                ", min([b, b]), b)
    check("max picks                ", max([b, b]), b)
    check("min over three           ", min([b, b + 1, b - 1]), b - 1)
    check("max over three           ", max([b, b + 1, b - 1]), b + 1)
    check("median of an odd count   ", median([b, b, b]), b)
    check("percentile on a point    ", percentile([b, b, b], 50), b)
    check("quantile on a point      ", quantile([b, b, b], 0.5), b)

    print "-- and `sort` ORDERS them, which it could not do at all"
    ' A SECOND ROUTE TO ONE QUESTION, FOUND BY MEASURING RATHER THAN BY READING.
    ' The exactness-aware comparison shipped in increment 2 reached `=` and `>`
    ' and NEVER REACHED `sort`, whose comparator compared doubles -- so two exact
    ' integers that round to the same double TIED, and a stable sort left them as
    ' it found them. `b > b - 1` answered true while `sort` put `b` first: two
    ' routes disagreeing, which is the shape PLAT-EQ needed three instalments to
    ' close for the KINDS and which had now happened once more for EXACTNESS.
    ' There is ONE comparison now and both routes go through it.
    pair = [b, b - 1]
    sort(pair)
    check("sort orders exactly      ", pair[0], b - 1)
    check("and the larger is second ", pair[1], b)

    print "-- exact in, exact out: abs, floor, ceil, round, sign"
    ' `floor` AND `ceil` ARE THE IDENTITY HERE and that is the whole fix: an
    ' exact integer is already its own floor, and the only reason it came back
    ' changed is that it was rounded to a double on the way in.
    check("abs                      ", abs(b), b)
    check("abs of a negative        ", abs(0 - b), b)
    check("floor is the identity    ", floor(b), b)
    check("ceil is the identity     ", ceil(b), b)
    check("round, no places         ", round(b), b)
    check("round with places        ", round(b, 2), b)
    check("sign                     ", sign(b), 1)

    print "-- `mod` on exact operands, which was a WRONG answer"
    check("parity of an odd integer ", mod(b, 2), 1)
    check("and of its neighbour     ", mod(b - 1, 2), 0)
    ' STILL FLOORED: the remainder takes the sign of the DIVISOR. Asserted on
    ' exact operands as well as the inexact ones in the controls below, because
    ' the two now take different code paths and a sign rule honoured on one is
    ' not evidence about the other.
    check("floored, negative left   ", mod(0 - 7, 3), 2)
    check("floored, negative right  ", mod(7, 0 - 3), -2)
    check("a divisor of -1          ", mod(b, 0 - 1), 0)

    print "-- `pow`, exact by repeated multiplication"
    check("past 2^53                ", pow(3, 34), 16677181699666569)
    check("2^62                     ", pow(2, 62), 4611686018427387904)
    check("a zero exponent          ", pow(7, 0), 1)
    check("a negative base          ", pow(0 - 2, 3), -8)
    ' SQUARE AND MULTIPLY, NOT A LOOP OVER THE EXPONENT: the exponent is an
    ' int64, so a loop would make this run for a thousand years instead of
    ' answering 1.
    check("a huge exponent of one   ", pow(1, number("1000000000000000000")), 1)

    print "-- `sum` is the fold of `+`, so it agrees with `+`"
    check("sum of one               ", sum([b]), b)
    check("sum carries the digit    ", sum([b, 1]), 9007199254740994)
    check("sum of many              ", sum([b, b, 0 - b]), b)

    print "-- the counting builtins are integers BY NATURE"
    ' ASSERTED AS ARITHMETIC, NOT AS A VALUE, and here that is forced rather than
    ' preferred: a count is small, so an exact 3 and an inexact 3 render
    ' IDENTICALLY and every value check passes on both builds. Only carrying the
    ' count into exact arithmetic separates them.
    '
    ' AND THE COUNT ITSELF HAS TO BE CHOSEN, which cost a perturbation to learn.
    ' Above 2^53 doubles step in TWOS, so adding 3 to the double view of b lands
    ' on the same answer the exact arithmetic gives -- `len([1,2,3]) + b` is
    ' ...996 on both builds. The first version of this tier used three-element
    ' lists and `"abc"` throughout and was therefore VACUOUS: reverting `len` to
    ' a double left every check green. It uses counts of ONE and TWO now, where
    ' the two arithmetics part company, which is the same lesson this file
    ' already records one section up -- assert the thing that DIFFERS, not the
    ' thing that looks like the subject.
    check("len of one               ", len([1]) + b, 9007199254740994)
    check("len of two               ", len([1, 2]) + b, 9007199254740995)
    check("len of a one-char string ", len("a") + b, 9007199254740994)
    check("count of a string        ", count("ab") + b, 9007199254740995)
    check("count of an array        ", count([1]) + b, 9007199254740994)
    check("count of a record        ", count({ x: 1, y: 2 }) + b, 9007199254740995)
    check("byte_count               ", byte_count("a") + b, 9007199254740994)
    check("a found index            ", find([7, 8], 8) + b, 9007199254740994)
    check("a found string index     ", find("abc", "c") + b, 9007199254740995)
    ' epoch IS THE ONE THAT MOTIVATES THE RULE: `epoch(b) - epoch(a)` is an
    ' interval a program does arithmetic with, so an inexact second count poisons
    ' every expression it reaches.
    ' THE ZONE IS NAMED, because the one-argument form reads the machine's local
    ' time and a constant written against it is a fact about this machine. 1791849600
    ' is 2026-10-13T00:00:00Z computed OUTSIDE gBASIC, in python's datetime.
    d {datetime}= "2026-10-13 00:00:00"
    check("epoch is exact           ", epoch(d, "UTC") + b, 9007201046590593)
    check("and differences are too  ", (epoch(d, "UTC") + b) - epoch(d, "UTC"), b)
    ' ALL THREE OF epoch's DOORS, because it has three return paths -- the
    ' CLOCK, a datetime in LOCAL time, and a datetime in a NAMED zone -- and a
    ' perturbation of the local one left this tier green while only the named one
    ' was asserted. Three paths, three checks, which is the rule this tree
    ' already applies to the two string-to-number doors.
    '
    ' THE OTHER TWO CANNOT BE PINNED BY VALUE: the clock moves and the local one
    ' is a fact about this machine's zone. So they are asserted as a DIFFERENCE,
    ' and DOUBLED first on purpose -- an even operand is representable above
    ' 2^53, so the broken arithmetic lands on ...992 whatever second it is run,
    ' where a bare `epoch()` would make the check depend on the parity of the
    ' current second and fail half the time.
    clock = epoch()
    check("the clock door is exact  ", (2 * clock + b) - 2 * clock, b)
    local = epoch(d)
    check("the local door is exact  ", (2 * local + b) - 2 * local, b)

    print "-- `decode` IS THE THIRD STRING-TO-NUMBER DOOR, and it was left behind"
    ' THE LESSON WAS ALREADY WRITTEN DOWN, one section up: "a rule re-established
    ' at two call sites is one a third site silently leaves out." `number("...")`
    ' and `{number}` were the two; this is the third. MEASURED 2026-10-13:
    ' `encode` wrote 9007199254740993 and `decode` read back ...992, so a value
    ' could be written to a file and could NOT BE READ BACK.
    '
    ' THE SHARPEST EVIDENCE THAT IT WAS A DEFECT rather than a decision:
    ' `stdlib/notation.bas` is pure gBASIC, parses its numbers with
    ' `number("...")`, and ROUND-TRIPPED THIS CORRECTLY THE WHOLE TIME. The
    ' library got it right by using the language's own door while the language's
    ' own decoder did not.
    check("the round trip holds     ", decode(encode(b)), b)
    check("and through an array     ", decode(encode([b]))[0], b)
    check("and through a record     ", decode(encode({ n: b })).n, b)
    check("decode of bare text      ", decode("9007199254740993"), b)
    check("a negative               ", decode("-9007199254740993"), 0 - b)
    check("try_decode agrees        ", try_decode("9007199254740993").value, b)
    ' EVERY NARROWING FALLS OUT OF THE SHARED HELPER rather than being restated,
    ' which is the whole reason for sharing it -- so each of these controls is a
    ' settled decision this door inherits rather than one it chose.
    check("an exponent stays inexact", decode("1e16") + 1 = 10000000000000001, false)
    check("a fraction stays inexact ", decode("0.5") + 0.25, 0.75)
    check("past int64 degrades      ", decode("99999999999999999999") > 0, true)
    check("and a string is a string ", decode("\"9007199254740993\""), "9007199254740993")
    ' `serialize` IS INCREMENT 4 AND STILL OPEN -- a NEGATIVE CONTROL, so it goes
    ' red when that increment lands and names itself as the line to update. It
    ' needs a SER_VERSION bump, the payload carrying only the double today.
    check("serialize still loses it ", deserialize(serialize(b)) = b, false)

    print "-- CONTROLS, each a settled decision rather than caution"
    ' WITHOUT THESE, "exactness reaches the builtins" is satisfied by calling
    ' everything exact -- which would be the same defect pointing the other way,
    ' since an inexact value claiming to be exact is a false claim about
    ' precision rather than a lost one.
    '
    ' `mean` DIVIDES, and `/` is always inexact -- including `6 / 3`. So mean is
    ' inexact by the rule already in force rather than by omission, and the same
    ' reading governs the EVEN case of `median`, which is the mean of two values.
    ' THESE TWO ARE NEGATIVE CONTROLS: they go red if anybody makes them exact,
    ' naming the decision to revisit.
    check("mean stays inexact       ", mean([b, b]) = b, false)
    check("an even median likewise  ", median([b, b]) = b, false)
    check("so does division         ", (b / 1) = b, false)
    ' AN INEXACT OPERAND CANNOT PRODUCE AN EXACT ANSWER, which is the half of the
    ' rule that stops exactness being invented.
    check("mixed min is inexact     ", min([b, b + 0.0]) = b, false)
    check("mod with an inexact side ", mod(b, 2.0) = 1, false)
    check("pow with a fraction      ", pow(2, 0.5), 1.4142135623730951)
    check("pow with a negative      ", pow(2, 0 - 1), 0.5)
    ' ROOTS AND LOGARITHMS ARE INEXACT BY NATURE and are deliberately untouched:
    ' `sqrt(4)` is 2 and detecting a perfect square is a different builtin.
    check("sqrt is untouched        ", sqrt(4), 2)
    check("log10 is untouched       ", log10(100), 2)
    ' AND THE ORDINARY ANSWERS DID NOT MOVE.
    check("ordinary min             ", min([3, 1, 2]), 1)
    check("ordinary max             ", max([3, 1, 2]), 3)
    check("ordinary median          ", median([4, 1, 2, 3]), 2.5)
    check("ordinary percentile      ", percentile([10, 20, 30], 75), 25)
    check("ordinary sum             ", sum([1, 2, 3]), 6)
    check("ordinary mean            ", mean([2, 4]), 3)
    check("a fractional floor       ", floor(0 - 2.1), -3)
    check("a fractional round       ", round(3.14159, 2), 3.14)
    check("round still rounds halves", round(2.5), 3)
    check("text min still picks     ", min(["b", "a"]), "a")

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
