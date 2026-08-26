' PLAT-LOOPSYN: the `next` terminator, named break/continue, compound
' assignment, and `default`. Golden-compared by tests/run_examples.sh.
'
' EVERY CHECK STATES ITS OWN EXPECTED ANSWER and prints ok or a MISMATCH naming
' both sides. That matters more than usual here, because three of these four
' features are SPELLINGS of things the language could already do: a golden
' alone would happily record `next` silently closing the wrong loop, or `+=`
' quietly disagreeing with `+`, as the expected output.

results = []

function check(label, expected, actual)
    if expected = actual then
        print "ok   " + label
        return true
    end if
    print "MISMATCH " + label + ": expected '" + string(expected) + "', got '" + string(actual) + "'"
    return false
end function

print "-- `next` closes a for loop, and `end for` still does"

seen = ""
for i = 1 to 3
    seen += string(i)
next i
append(results, check("next NAME closes the loop", "123", seen))

seen = ""
for i = 1 to 3
    seen += string(i)
next
append(results, check("bare next closes it too", "123", seen))

seen = ""
for i = 1 to 3
    seen += string(i)
end for
append(results, check("and end for is unchanged", "123", seen))

seen = ""
for each letter in ["a", "b", "c"]
    seen += letter
next letter
append(results, check("for each takes the element name", "abc", seen))

' The point of spelling the terminator `next` rather than reserving a word:
' `next` remains an ordinary variable. (It cannot be a FOR variable, which
' takes a bare IDENT -- that predates this feature and is unchanged.)
next = 41
next += 1
append(results, check("`next` is still an ordinary variable name", 42, next))
loop = "loop"
until = "until"
append(results, check("so are `loop` and `until`", "loopuntil", loop + until))

print ""
print "-- break and continue can name the loop they mean"

' The classic nested-loop shape: abandon the inner loop and take the next
' iteration of the outer one.
pairs = ""
for x = 1 to 4
    for y = 4 to 1 step -1
        if y = x then continue x
        pairs += string(x) + string(y) + " "
    next y
next x
append(results, check("continue NAME resumes the named loop", "14 13 12 24 23 34 ", pairs))

' break leaves the named loop entirely -- the case `next x` could never express.
pairs = ""
for x = 1 to 3
    for y = 1 to 3
        if x = 2 and y = 2 then break x
        pairs += string(x) + string(y) + " "
    next y
next x
append(results, check("break NAME leaves the named loop", "11 12 13 21 ", pairs))

' Unnamed forms are untouched: they still mean the innermost loop.
pairs = ""
for x = 1 to 2
    for y = 1 to 3
        if y = 2 then break
        pairs += string(x) + string(y) + " "
    next y
next x
append(results, check("bare break still means the innermost", "11 21 ", pairs))

' A named flow travels PAST a while loop, which has no variable to name.
pairs = ""
for x = 1 to 3
    n = 0
    while n < 5
        n += 1
        if n = 2 then continue x
        pairs += string(x) + string(n) + " "
    end while
next x
append(results, check("a named flow passes through a while", "11 21 31 ", pairs))

print ""
print "-- compound assignment is exactly `x = x op e`"

n = 10
n += 5
append(results, check("+= on a number", 15, n))
n -= 3
append(results, check("-= on a number", 12, n))
n *= 2
append(results, check("*= on a number", 24, n))
n /= 4
append(results, check("/= on a number", 6, n))

s = "ab"
s += "cd"
append(results, check("+= concatenates strings", "abcd", s))

d{date}= "2026-08-25"
d += 3 days
append(results, check("+= adds a duration to a date", "2026-08-28", string(d)))

cost{USD}= 10.50
more{USD}= 5.25
cost += more
append(results, check("+= adds money to money", "15.75", string(cost)))

span = 3 days
span *= 2
append(results, check("*= scales a duration", "6 days", string(span)))

rec = { count: 1 }
rec.count += 41
append(results, check("+= through a field", 42, rec.count))

arr = [10, 20]
arr[1] += 5
append(results, check("+= through an index", 25, arr[1]))

' The modifier applies to the RESULT, exactly as in a plain modified
' assignment: fold first, then transform.
name = "ab"
name{upper} += "cd"
append(results, check("a modifier transforms the folded result", "ABCD", name))

' Evaluated once as a statement, but the target is read and then written --
' the same two touches `x = x + 1` makes.
counter = 0
counter += 1
counter += 1
append(results, check("repeated compound assignment accumulates", 2, counter))

print ""
print "-- default(value, fallback)"

append(results, check("an unset variable falls back", "fallback", default(env("GBASIC_NO_SUCH_VAR"), "fallback")))
append(results, check("a real value is kept", "real", default("real", "fallback")))
append(results, check("nothing falls back", "gave", default(nothing, "gave")))
append(results, check("a find miss falls back", -1, default(find(["a"], "zzz"), -1)))
append(results, check("a find hit is kept", 1, default(find(["a", "b"], "b"), -1)))

' Presence, not truthiness: a value that IS there stays, however falsy.
append(results, check("false is a value", false, default(false, "gave")))
append(results, check("zero is a value", 0, default(0, "gave")))
append(results, check("the empty string is a value", "", default("", "gave")))

bad = 0
for each verdict in results
    if not verdict then
        bad += 1
    end if
next verdict

print ""
print "checks: " + string(count(results))
print "mismatches: " + string(bad)
