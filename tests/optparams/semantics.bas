' PLAT-OPTPARAM: literal default values for function parameters.
'
' SELF-CHECKING, not a transcript. A default that silently fails to apply
' yields `nothing`, and `nothing` prints as an ordinary word -- a golden would
' record it as the expected output and defend it. Every line states its own
' expected answer.

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

' ------------------------------------------------- every literal kind
function kinds(a, n = 42, neg = -7, f = 1.5, s = "text", t = true, fa = false, no = nothing, un = unknown)
    return (string(n) + "|" + string(neg) + "|" + string(f) + "|" + s + "|"
            + string(t) + "|" + string(fa) + "|" + string(no) + "|" + string(un))
end function

check("every literal kind defaults", kinds(0),
      "42|-7|1.5|text|true|false|nothing|unknown")

' ------------------------------------------------------- partial supply
function three(a, b = "B", c = "C")
    return a + b + c
end function

check("none of the optionals supplied", three("A"), "ABC")
check("one supplied", three("A", "b"), "AbC")
check("all supplied", three("A", "b", "c"), "Abc")

' Supplying a value that EQUALS the default must be indistinguishable.
check("supplying the default explicitly", three("A", "B", "C"), "ABC")

' NOTE the classic mutable-default bug cannot arise here. An array or record
' default is a PARSE ERROR (only literals are accepted), and gBASIC strings are
' immutable, so no default is a value that could be mutated and carried into
' the next call. The refusals are pinned in tests/optparams/refuse_*.bas.

' ------------------------------------------------------------ recursion
function countdown(n, depth = 0)
    if n <= 0 then
        return depth
    end if
    return countdown(n - 1, depth + 1)
end function

check("defaults work under recursion", countdown(5), 5)

' -------------------------------------------------- through a function VALUE
function scaled(x, by = 10)
    return x * by
end function

fv = scaled
check("function value, default applied", fv(3), 30)
check("function value, default overridden", fv(3, 2), 6)

' ------------------------------------------------------- inside a library
' (finance is loaded only to prove an ordinary call still works unchanged.)
check("an all-required function is unaffected", three("1", "2", "3"), "123")

' ------------------------------------------------------------- refusals
on error goto next

x = three()
check("too few arguments still raises", error.message,
      "three expects 1 to 3 arguments, got 0")
error.clear()

y = three("a", "b", "c", "d")
check("too many arguments still raises", error.message,
      "three expects 1 to 3 arguments, got 4")
error.clear()

' An all-required function keeps the ORIGINAL message, with no range in it.
function exact(a, b)
    return a
end function

z = exact(1)
check("an all-required function keeps the exact-count message", error.message,
      "exact expects 2 arguments, got 1")
error.clear()

on error stop

print "checks: " + string(tally.checks)
print "mismatches: " + string(tally.mismatches)
