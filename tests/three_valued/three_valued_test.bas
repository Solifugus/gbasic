' THREE-VALUED LOGIC -- docs/unknown_nan_design.md, increment B.
' Self-checking; run by tests/run_three_valued.sh.
'
' `unknown` is gBASIC's SQL NULL, so its truth tables are SQL's: Kleene `not`,
' `and`, `or`, and `unknown` propagating through comparison and arithmetic.
' THE TABLES BELOW ARE WRITTEN OUT BY HAND from SQL's definition, not read back
' from the implementation -- a table copied from what the binary answers is a
' transcript and would defend whatever it said.
'
' What does NOT change is asserted beside it, because "unknown propagates" is
' otherwise satisfied by a build where it propagates everywhere: `if`, `while`
' and `until` still refuse an unknown, the "same value" routes still call
' unknown the same value as unknown, string `+` still renders it (D2), and
' `nothing` stays two-valued (D3).

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

function name_of(v)
    if is_unknown(v) then
        return "unknown"
    end if
    return string(v)
end function

' Counts how often the right operand was evaluated, so the short-circuit rule
' is observed rather than inferred.
function touch()
    tally.touched = tally.touched + 1
    return true
end function

function boom()
    error "the right operand was evaluated"
    return true
end function

' Does `if c` raise? (It must, for an unknown.)
function if_refuses(c)
    on error goto next
    if c then
        x = 1
    end if
    if error then
        return true
    end if
    return false
end function

function while_refuses(c)
    on error goto next
    while c
        c = false
    end while
    if error then
        return true
    end if
    return false
end function

function until_refuses(c)
    on error goto next
    do
        x = 1
    until c
    if error then
        return true
    end if
    return false
end function

program main()
    tally = { checks: 0, mismatches: 0, touched: 0 }
    t3 = [true, false, unknown]

    print "-- NOT (SQL: not T = F, not F = T, not U = U)"
    want_not = [false, true, unknown]
    for each a, i in t3
        check("not " + name_of(a), not a, want_not[i])
    end for

    print ""
    print "-- AND, rows are the left operand, columns the right, both T F U"
    want_and = [[true, false, unknown], [false, false, false], [unknown, false, unknown]]
    for each a, i in t3
        for each b, j in t3
            check(name_of(a) + " and " + name_of(b), a and b, want_and[i][j])
        end for
    end for

    print ""
    print "-- OR"
    want_or = [[true, true, true], [true, false, unknown], [true, unknown, unknown]]
    for each a, i in t3
        for each b, j in t3
            check(name_of(a) + " or " + name_of(b), a or b, want_or[i][j])
        end for
    end for

    print ""
    print "-- SHORT-CIRCUIT ONLY WHERE THE LEFT DETERMINES THE ANSWER"
    ' The right operand here is a RAISE, so the only way these pass is by never
    ' looking at it.
    check("false and <raise> is false", false and boom(), false)
    check("true or <raise> is true", true or boom(), true)
    tally.touched = 0
    r = unknown and touch()
    check("unknown and ... evaluates the right", tally.touched, 1)
    check("  and answers unknown when it is true", r, unknown)
    r = unknown or touch()
    check("unknown or ... evaluates the right", tally.touched, 2)
    check("  and answers true when it is true", r, true)
    r = true and touch()
    check("true and ... evaluates the right", tally.touched, 3)
    r = false or touch()
    check("false or ... evaluates the right", tally.touched, 4)
    check("unknown and false is false: commutative", unknown and false, false)
    check("unknown or true is true: commutative", unknown or true, true)

    print ""
    print "-- ORDINARY TRUTHINESS IS UNCHANGED for operands that are not unknown"
    check("1 and 2", 1 and 2, true)
    check("0 or \"\"", 0 or "", false)
    check("\"x\" and unknown", "x" and unknown, unknown)
    check("0 and unknown", 0 and unknown, false)
    check("[] or unknown", [] or unknown, unknown)

    print ""
    print "-- COMPARISON PROPAGATES unknown"
    check("unknown = 1", unknown = 1, unknown)
    check("1 = unknown", 1 = unknown, unknown)
    check("unknown = unknown", unknown = unknown, unknown)
    check("unknown != unknown", unknown != unknown, unknown)
    check("unknown != 1", unknown != 1, unknown)
    check("unknown > 1", unknown > 1, unknown)
    check("1 <= unknown", 1 <= unknown, unknown)
    check("unknown !> 1", unknown !> 1, unknown)
    check("\"a\" = unknown", "a" = unknown, unknown)
    check("a date against unknown", {date}"2026-01-01" < unknown, unknown)
    check("money against unknown", {USD}5 = unknown, unknown)
    check("a record against unknown", {a: 1} = unknown, unknown)
    check("unknown = nothing (D3: one side is unknown)", unknown = nothing, unknown)
    check("compare() follows the operator", compare(unknown, "=", 1), unknown)

    print ""
    print "-- ARITHMETIC PROPAGATES unknown"
    check("unknown + 1", unknown + 1, unknown)
    check("1 - unknown", 1 - unknown, unknown)
    check("unknown * 0", unknown * 0, unknown)
    check("0 / unknown", 0 / unknown, unknown)
    check("-unknown", -unknown, unknown)
    check("money + unknown", {USD}5 + unknown, unknown)
    check("date + unknown", {date}"2026-01-01" + unknown, unknown)
    check("unknown + unknown", unknown + unknown, unknown)

    print ""
    print "-- STRING + STILL RENDERS IT (D2)"
    check("\"a\" + unknown", "a" + unknown, "aunknown")
    check("unknown + \"a\"", unknown + "a", "unknowna")

    print ""
    print "-- NOTHING STAYS TWO-VALUED (D3)"
    check("nothing = nothing", nothing = nothing, true)
    check("not nothing", not nothing, true)
    check("nothing = 1", nothing = 1, false)
    check("nothing and true", nothing and true, false)

    print ""
    print "-- CONTROL FLOW STILL REFUSES AN UNKNOWN"
    check("if unknown raises", if_refuses(unknown), true)
    check("if (unknown = 1) raises", if_refuses(unknown = 1), true)
    check("while unknown raises", while_refuses(unknown), true)
    check("until unknown raises", until_refuses(unknown), true)
    check("the control: if false does not", if_refuses(false), false)
    check("and if (unknown and false) is decided", if_refuses(unknown and false), false)

    print ""
    print "-- THE ROUTES STILL CALL unknown THE SAME VALUE AS unknown"
    check("contains([unknown], unknown)", contains([unknown], unknown), true)
    check("contains([1], unknown)", contains([1], unknown), false)
    check("contains([unknown], 1)", contains([unknown], 1), false)
    check("find([1, unknown], unknown)", find([1, unknown], unknown), 1)
    check("unique([unknown, unknown, 1])", unique([unknown, unknown, 1]), [unknown, 1])
    check("remove_value([unknown, 1], unknown)", remove_value([unknown, 1], unknown), [1])
    check("excluding", [unknown, 1] excluding [unknown], [1])
    check("compound = keeps the same-value answer", [unknown] = [unknown], true)
    check("and a record", {a: unknown} = {a: unknown}, true)
    check("and is false when only one side is unknown", [unknown] = [1], false)
    hit = "none"
    consider unknown
    if 1 then
        hit = "one"
    if unknown then
        hit = "unknown"
    end consider
    check("consider matches an unknown branch", hit, "unknown")
    check("is_unknown is unchanged", is_unknown(unknown), true)
    check("  and on an ordinary value", is_unknown(0), false)

    print ""
    print "-- THE IDIOM THAT MOVED: a missing key, read and compared"
    r = { a: 1 }
    check("r[\"b\"] = 1 is unknown, not false", r["b"] = 1, unknown)
    check("is_unknown(r[\"b\"]) is the question to ask", is_unknown(r["b"]), true)

    print "ok   " + string(tally.checks) + " checks, " + string(tally.mismatches) + " mismatches"
end program
