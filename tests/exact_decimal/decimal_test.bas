' EXACT DECIMALS (docs/exact_decimal_design.md, increment 1; book gap G50).
'
' A number written or read with a decimal point is exact, as integers already
' are: `+`, `-` and `*` stay exact, and division and functions are the double.
' SELF-CHECKING, every answer worked by hand. The answers were all wrong before
' by a digit far out, which a golden would have recorded as expected.

function check(label, got, want)
    if string(got) = string(want) then
        print "ok   " + label
    else
        print "MISMATCH " + label + ": got " + string(got) + ", want " + string(want)
    end if
    return nothing
end function

program main( args )
    print "-- the book's cases (G50): these drifted"
    check("a quantity difference    ", 6331.475 - 2182.855, "4148.62")
    check("a short difference       ", 412.34 - 412.3350, "0.005")
    check("from text                ", number("6331.475") - number("2182.855"), "4148.62")
    q = {number}"0.0001"
    check("from the modifier        ", q * 3, "0.0003")
    check("from decode              ", decode("[1.10, 2.5]")[0] * 3, "3.3")

    print "-- comparison: exact against exact is mathematical"
    check("0.1 + 0.2 = 0.3          ", 0.1 + 0.2 = 0.3, true)
    check("past a double's digits   ", 0.1 < 0.10000000000000001, true)
    check("a decimal against an int ", 2.5 > 2, true)
    check("4 = 4.0                  ", 4 = 4.0, true)
    check("2^53 + 1 is not 2^53.0   ", 9007199254740993 = 9007199254740992.0, false)
    print "-- and against an INEXACT number, as doubles"
    ' sqrt(0.01) is the nearest double to 0.1; the literal is exactly 0.1. As
    ' doubles they are one value, which is what every such test meant.
    check("sqrt(0.01) = 0.1         ", sqrt(0.01) = 0.1, true)
    check("a double sum stays one   ", 1e-1 + 2e-1 = 0.3, false)

    print "-- arithmetic"
    check("product scales add       ", 1.25 * 0.04, "0.05")
    check("whole product            ", 1.5 * 2, "3")
    check("negation                 ", -(0.25 * 4.4), "-1.1")
    check("a negative literal       ", -0.05 + 0.02, "-0.03")
    check("small                    ", 0.000001 + 0, "0.000001")
    check("eighteen places          ", 0.123456789012345678 + 0, "0.123456789012345678")
    check("division is the double   ", 1.0 / 3, "0.3333333333333333")
    check("a double makes a double  ", 0.1 + 2e-1, "0.30000000000000004")
    check("sum of a column          ", sum([0.1, 0.2, 0.3]), "0.6")
    check("sum with an integer      ", sum([1, 0.25]), "1.25")
    check("compounding (pow)        ", pow(1.05, 3), "1.157625")
    check("pow past 18 places       ", round(pow(1.005, 12), 6), "1.061678")

    print "-- picks and rounds keep it"
    check("round half away          ", round(2.675, 2), "2.68")
    check("round negative half      ", round(-2.5), "-3")
    check("round to more places     ", round(0.5, 3), "0.5")
    check("floor of a negative      ", floor(-1.5), "-2")
    check("ceil                     ", ceil(1.2), "2")
    check("abs                      ", abs(-0.75), "0.75")
    check("sign                     ", sign(-0.1), "-1")
    check("min picks the decimal    ", min([0.3, 0.1 + 0.2, 0.2]), "0.2")
    check("sort orders exactly      ", sort([0.3, 0.25, 0.1 + 0.2]), "[0.25,0.3,0.3]")
    check("unique merges equal ones ", len(unique([0.1 + 0.2, 0.3, 0.30])), 1)

    print "-- the loop counter"
    last = 0
    n = 0
    for x = 0 to 1 step 0.1
        last = x
        n = n + 1
    end for
    check("step 0.1 ends on 1       ", last, "1")
    check("and runs eleven times    ", n, 11)
    check("and 1 is reached exactly ", last = 1, true)

    print "-- a decimal is never the exact integer by itself"
    ' `1.0` is how code has said "a double" (frame.bas did); it is exact now,
    ' and still not an integer, so `pow(2.0, 64)` is the double with no warning.
    check("pow of a whole decimal    ", pow(2.0, 64), "1.8446744073709552e+19")
    check("negative zero survives    ", string(-0.0), "-0")
    check("rendered as an integer    ", 3.0, "3")

    print "-- text and encoding"
    check("string                   ", string(-0.05), "-0.05")
    check("encode                   ", encode({ a: 0.1 + 0.2 }), "{\"a\":0.3}")
    check("json_encode              ", json_encode([1.25 * 4, 0.3]), "[5,0.3]")
    y:number = "0.7"
    check("a typed lock from text   ", y - 0.5, "0.2")
end program
