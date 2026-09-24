' THE FIRST RAISE WINS while one is still unwinding -- the other half of
' PLAT-ERR's anti-silence pair. Rule 1 says a second raise may not SHADOW a
' pending one inside a frame; this says it may not REPLACE one on its way out.
'
' DOGFOOD 34. Before 2026-09-23 the cause you were shown depended on whether
' the enclosing caller happened to check `error_action_pending` after
' evaluating an argument. Measured on 0.2.2 with a missing-file read in five
' positions, two reported the missing file and three reported that the outer
' builtin wanted a string -- a complaint about the symptom the discarded raise
' had just caused, naming the one call that is NOT at fault. Nothing in the
' source said which you would get, and `decode(read(f))` is not an exotic
' shape: every read-then-decode in the book had to be split into two
' statements to get a usable message.
'
' THE FIVE POSITIONS ARE ASSERTED TOGETHER, because the defect was that they
' DISAGREED. Any one of them alone passes on a build that is consistently
' wrong; what makes the rule a rule is that arithmetic, a user function and
' three different builtins now all name the same cause.
'
' Lines say ok or WRONG rather than merely existing, so a regression names
' itself instead of quietly moving a golden.

function mine(x)
    return x
end function

missing {file}= "tests/error_model/no_such_file_exists_here.json"

on error goto next

' ------------------------------------------------- the three that were WRONG
a = decode(read(missing))
if contains(error.message, "could not read file") then
    print "ok   decode(read(f))     reports the read"
else
    print "WRONG: decode(read(f)) reported " + error.message
end if
error.clear()

b = try_decode(read(missing))
if contains(error.message, "could not read file") then
    print "ok   try_decode(read(f)) reports the read"
else
    print "WRONG: try_decode(read(f)) reported " + error.message
end if
error.clear()

c = len(read(missing))
if contains(error.message, "could not read file") then
    print "ok   len(read(f))        reports the read"
else
    print "WRONG: len(read(f)) reported " + error.message
end if
error.clear()

' ------------------------------------------- the two that were already right
' Kept, and not as decoration: they are the half that says nothing MOVED. A
' fix that made every nested raise report some other fixed thing would pass
' the three above and fail here.
d = 1 + read(missing)
if contains(error.message, "could not read file") then
    print "ok   1 + read(f)         reports the read"
else
    print "WRONG: 1 + read(f) reported " + error.message
end if
error.clear()

e = mine(read(missing))
if contains(error.message, "could not read file") then
    print "ok   mine(read(f))       reports the read"
else
    print "WRONG: mine(read(f)) reported " + error.message
end if
error.clear()

' --------------------------------------------------- THREE DEEP, not just one
g = string(len(read(missing)))
if contains(error.message, "could not read file") then
    print "ok   and it survives two enclosing calls"
else
    print "WRONG: nested twice reported " + error.message
end if
error.clear()

' ------------------------------------------- THE LOAD-BEARING CONTROL
' Without this, "the first raise wins" is satisfied by a build that stopped
' reporting builtin type errors AT ALL. With nothing in flight, each of the
' three must still complain in its own words about its own argument.
h = decode(42)
if contains(error.message, "decode expects a string") then
    print "ok   decode(42) still says what decode wants"
else
    print "WRONG: decode(42) reported " + error.message
end if
error.clear()

i = len(42)
if contains(error.message, "len expects string or array") then
    print "ok   len(42) still says what len wants"
else
    print "WRONG: len(42) reported " + error.message
end if
error.clear()

j = try_decode(42)
if contains(error.message, "try_decode expects a string") then
    print "ok   try_decode(42) still says what try_decode wants"
else
    print "WRONG: try_decode(42) reported " + error.message
end if
error.clear()

' ------------------------------------ THE SECOND CONTROL: the flight is CLEARED
' An absorbed error is not in flight, so the NEXT raise is a first raise. If
' the guard leaked past absorption, every error after the first in a session
' would report the first one's message forever.
k = decode("{\"a\": 1}")
if error then
    print "WRONG: a healthy decode raised " + error.message
else
    print "ok   and a healthy call after all that still works: " + string(k.a)
end if
error.clear()

' --------------------------------------- AND NOTHING ACTED ON THE BAD ARGUMENT
' The guard makes the MESSAGE right; this says the builtin did not also do its
' job on a value it should never have been handed. `append` writes through a
' reference, so a failed argument that got through would leave the caller's
' array one element longer with nothing to show for it.
xs = [1, 2]
append(xs, read(missing))
if contains(error.message, "could not read file") then
    print "ok   append(xs, read(f)) reports the read"
else
    print "WRONG: append reported " + error.message
end if
error.clear()
if count(xs) = 2 then
    print "ok   and the array was not appended to"
else
    print "WRONG: the array grew to " + string(count(xs))
end if
