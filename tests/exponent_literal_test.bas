' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' Scientific notation as a LITERAL, and the silent trap that made its absence
' expensive.
'
' `1e20` used to lex as the number 1 beside the identifier `e20`, which the
' grammar reads as a DURATION -- and an unknown duration unit did not refuse.
' It printed an UNLOCATED line straight to stderr, bypassing the diagnostics
' sink, answered `0 seconds`, and EXITED 0. So the missing literal did not
' merely fail to parse: it put a plausible duration where a large number was
' written. `1 fortnight` had the same shape and nothing downstream could see
' it.
'
' SELF-CHECKING rather than golden, and that is forced on the second half: the
' old behaviour produced `0 seconds`, which is a perfectly ordinary duration a
' golden would have recorded as expected. The refusals are asserted in the
' runner, because a program that will not parse cannot check itself.

tally = { checks: 0, mismatches: 0 }

function check(label, got, want)
    tally.checks = tally.checks + 1
    if string(got) = string(want) then
        print "ok   " + label
    else
        tally.mismatches = tally.mismatches + 1
        print "MISMATCH " + label + ": got " + string(got) + ", want " + string(want)
    end if
    return nothing
end function

print "-- the literal"
check("a positive exponent", 1e3, 1000)
check("capital E", 2E10, 20000000000)
check("an explicit plus", 1e+3, 1000)
check("a negative exponent", 1.5e-3, 0.0015)
check("a fraction and an exponent", 6.02e23, 602000000000000000000000)
check("a leading fraction", 0.5e-2, 0.005)

print ""
print "-- THE ORACLE: the literal and the text route land on the SAME double"
' This is the tier that says the lexer hands the same bytes to strtod that
' `number()` does. A value check alone passes on a lexer that is consistently
' off by a factor it also applies to the expected literal, and there is no
' expected literal here -- the two routes are compared against each other, and
' the second one is the route the whole tree has been using instead.
check("1e20", 1e20 = number("1e20"), true)
check("-1.2345678901234567e-308", -1.2345678901234567e-308 = number("-1.2345678901234567e-308"), true)
check("6.02e23", 6.02e23 = number("6.02e23"), true)
check("1e-5", 1e-5 = number("1e-5"), true)

print ""
print "-- and it is a NUMBER, not a duration and not text"
check("the type", type(1e20), "number")
check("it does arithmetic", 1e3 + 1, 1001)
check("it renders as the shortest round trip", string(1e20), "1e+20")

print ""
print "-- CONSERVATIVE: what did NOT become a number"
' Without these, "scientific notation is a literal" is satisfied by a lexer
' that swallowed the identifier after every number.
check("hex is untouched", 0x1e, 30)
check("a duration still parses", string(2 hours 30 minutes), "2 hours 30 minutes")
check("a space keeps them apart", 1 + 1, 2)
e2 = 7
check("a variable whose name starts with e", 1 * e2, 7)

print ""
print "checks: " + string(tally.checks)
print "mismatches: " + string(tally.mismatches)
