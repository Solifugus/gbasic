' TYPED PARAMETERS AND RETURN TYPES (docs/typed_variables_design.md rules 8
' and 9). Self-checking; run by tests/run_typed_locks.sh.
'
' `function f(a:number)` converts each argument as an assignment would, and
' the parameter stays locked in the body. `function f(...):number` converts
' every `return` with a value, and refuses at the `return`. Each check sits
' beside the same function without the type, so a type that did nothing shows.

tally = { checks: 0, mismatches: 0 }

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

' What a call raised (with the line it was raised at), or ANSWERED.
function refusal(f, arg)
    on error goto handle
    r = f(arg)
    return "ANSWERED " + string(r)
handle:
    m = error.message + " @" + string(error.line)
    error.clear()
    return m
end function

print "-- A PARAMETER'S LOCK CONVERTS ITS ARGUMENT"
function twice(n:number)
    return n * 2
end function
function twice_plain(n)
    return n + n
end function
check("twice(\"21\") with n:number is 42", twice("21"), 42)
check("  control: with no lock, \"21\" + \"21\" is text", twice_plain("21"), "2121")

function relabel(a:number)
    a = " 7 "
    return a
end function
check("the parameter stays locked in the body", relabel(1), 7)

function on_day(when:date)
    return when + 1 day
end function
check("a date parameter from text", on_day("2026-01-31"), {date}"2026-02-01")

function priced(p:money(USD), q:number = "2")
    return p * q
end function
check("a default converts too: q:number = \"2\"", priced("1.50"), {USD}"3.00")
check("  and a supplied argument beside it", priced("1.50", "3"), {USD}"4.50")

function absent(n:number)
    return n
end function
check("nothing passes into a locked parameter", absent(nothing), nothing)

function local_only(n:number)
    return n
end function
outer = "5"
r = local_only(outer)
check("the caller's variable keeps its own value", outer, "5")

function bad_arg(n:number)
    return n
end function
check("an argument that does not convert is refused at the call",
      refusal(bad_arg, "abc"),
      "n is locked to number by n:number at line 72, and this value does not convert: number modifier expects a valid number string @25")

print ""
print "-- A RETURN TYPE CONVERTS EVERY RETURN"
function total(items):money(USD)
    t = 0
    for each i in items
        t = t + number(i)
    end for
    return string(t)
end function
function total_plain(items)
    t = 0
    for each i in items
        t = t + number(i)
    end for
    return string(t)
end function
check("total(...):money(USD) returns money", total(["1.25", "2"]), {USD}"3.25")
check("  control: with no return type it is text", total_plain(["1.25", "2"]), "3.25")

function early(flag):number
    if flag then
        return "5"
    end if
    return "6"
end function
check("an early return converts", early(true), 5)
check("  and the last one", early(false), 6)

function falls_off(flag):number
    if flag then
        return "5"
    end if
end function
check("falling off the end returns nothing", falls_off(false), nothing)

function says_nothing(x):number
    return nothing
end function
check("return nothing is allowed", says_nothing(1), nothing)

counts = {}
function counts.size():number
    return "3"
end function
check("a dotted function's return type", counts.size(), 3)

function bad_return(x):number
    return "n/a"
end function
check("a return that does not convert is refused AT the return",
      refusal(bad_return, 1),
      "bad_return returns number, declared by bad_return(...):number at line 125, and this value does not convert: number modifier expects a valid number string @126")

print ""
print "-- ONLY THE FUNCTION'S OWN RETURN"
modifier doubled for assign
    return value + value
end modifier
function uses_modifier(x):string
    y {doubled}= 2
    return type(y)
end function
check("a modifier's return inside a typed function keeps its own type", uses_modifier(1), "number")

modifier near for compare
    return abs(left - right) < 1
end modifier
function compares_near(x):number
    b = (x {near}= 1.5)
    if type(b) = "boolean" then
        return 1
    end if
    return 0
end function
check("a compare modifier's return inside a typed function keeps its own type", compares_near(2), 1)

function inner():number
    return "1"
end function
function outer_fn():string
    v = inner()
    return type(v)
end function
check("a nested call's return type is its own", outer_fn(), "number")

print ""
print "checks: " + string(tally.checks)
print "mismatches: " + string(tally.mismatches)
