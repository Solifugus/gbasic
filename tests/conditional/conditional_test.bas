' `a if c else b` (Matthew, 2026-11-20): self-checking. Every check prints
' `ok` or `MISMATCH`; tests/run_conditional.sh counts them.

function check(label, got, want)
    if string(got) = string(want) then
        print "ok   " + label
    else
        print "MISMATCH " + label + ": got " + string(got) + ", want " + string(want)
    end if
    return nothing
end function

function kind_of(n)
    return "one" if n = 1 else "many"
end function

' a write to a record parameter, then read in a conditional: not warning 2125
function stamp(p, keep)
    p.seen = true
    return p if keep else { seen: false }
end function

program main(args)
    x = 5
    check("the value before if, when true", "big" if x > 3 else "small", "big")
    check("the value after else, when false", "big" if x > 9 else "small", "small")
    check("a chain groups to the right", "low" if x < 3 else "mid" if x < 10 else "high", "mid")
    check("a chain's last value", "low" if x < 3 else "mid" if x < 4 else "high", "high")

    ' LOOSER THAN `or`: the condition and the first value are whole or-expressions
    check("the condition takes an or", 1 if false or true else 2, 1)
    check("the first value takes an or", false or true if true else false, true)
    check("else takes all of 2 + 10", 1 if false else 2 + 10, 12)
    check("and not just the 2", 1 if true else 2 + 10, 1)
    check("parentheses make it a term", (1 if false else 2) + 10, 12)

    ' `try` still binds loosest: it covers the conditional, not one value
    check("try covers the whole", try 1 / 0 if false else 7, 7)

    ' ONLY THE CHOSEN VALUE IS EVALUATED: the other one would raise
    on error goto next
    v = 5 if true else 1 / 0
    check("the else value is not evaluated", v, 5)
    check("and raised nothing", error, false)
    w = 1 / 0 if false else 6
    check("the first value is not evaluated", w, 6)
    check("and raised nothing either", error, false)
    r = {}
    check("x.name if x else ...: x.name not read", r.name if r else "none", "none")

    ' THE CONDITION IS `if`'s: the same truth, and unknown refused, not false
    check("0 is false", "t" if 0 else "f", "f")
    check("an empty string is false", "t" if "" else "f", "f")
    check("an empty list is false", "t" if [] else "f", "f")
    check("a list with one item is true", "t" if [0] else "f", "t")
    u = "unset"
    u = "t" if unknown else "f"
    check("unknown is refused: neither value assigned", u, "unset")
    check("and it raised, code 1003", error.code, 1003)
    error.clear()

    ' WHERE AN EXPRESSION GOES
    check("in a list", [1 if x = 5 else 0, 2], [1, 2])
    check("in a record", { k: "a" if x = 5 else "b" }, { k: "a" })
    check("as an argument", len("abc" if x = 5 else "a"), 3)
    check("from a return", kind_of(1) + " " + kind_of(2), "one many")
    if x = 5 then z = "a" if x > 1 else "b"
    check("inside a one-line if", z, "a")
    if x = 4 then z = "a" if x > 1 else "b"
    else z = "c" if x > 1 else "d"
    check("and its else on the next line", z, "c")
    s = 0
    s += 10 if x = 5 else 1
    check("a compound assignment", s, 10)

    ' BRACKETS ON PLAIN DATA: a row's field in the condition, per row
    rows = [{ k: 1, n: "a" }, { k: 5, n: "b" }, { k: 9, n: "c" }]
    big = rows[("hi" if .k > 4 else "lo") = "hi"]
    check("a row's field in the condition", big.n, ["b", "c"])

    check("a record parameter read in a conditional", stamp({ seen: false }, true).seen, true)
end program
