' `lib.fn` used as a VALUE, not just called.
'
' A bare `fn` has evaluated to a function value since first-class functions
' landed, and the value has always carried a library -- but the QUALIFIED
' spelling was recognised only in call position. `lib.fn(x)` worked while
' `f = lib.fn` died with "undefined variable: lib", so passing a library
' function as a callback had no direct form: the workaround was a record
' carrying state and function together, invoked as a method, which drags
' otherwise-private wiring into the caller purely for reachability.
'
' Reported from the Secure File Transfer Manager build, 2026-08-28.

load heartbeat from "libs/heartbeat.bas"

results = []

function check(label, expected, actual)
    if expected = actual then
        print "ok   " + label
        return true
    end if
    print "MISMATCH " + label + ": expected '" + string(expected) + "', got '" + string(actual) + "'"
    return false
end function

function run_twice(cb)
    return cb(10) + "/" + cb(20)
end function

' Calling still works exactly as before -- this is an addition, not a change.
append(results, check("qualified call", "tick 1", heartbeat.tick(1)))

' The value, and the four things you would do with it.
f = heartbeat.tick
append(results, check("qualified name is a function value", "function", type(f)))
append(results, check("the value calls", "tick 2", f(2)))
append(results, check("passes as a callback", "tick 10/tick 20", run_twice(heartbeat.tick)))
reg = { beat: heartbeat.tick, name: heartbeat.label }
append(results, check("stores in a record", "tick 3", reg.beat(3)))
append(results, check("a second one, no arguments", "heartbeat", reg.name()))

' An array of them, which is what a dispatch table is. NOTE the binding:
' `table[0](7)` does not parse -- calling a function value straight out of a
' SUBSCRIPT is a separate, pre-existing limitation, and unrelated to this
' change. A FIELD does work directly (`reg.beat(3)` above).

' (Named `beat_fn`, not `first`: `first` is a BUILTIN, and a call resolves to
' the builtin rather than to a same-named variable holding a function value --
' the collision hazard, met while writing this test.)
table = [heartbeat.tick, heartbeat.tick]
beat_fn = table[0]
append(results, check("stores in an array", "tick 7", beat_fn(7)))

' SHADOWING IS UNCHANGED. A variable of the library's name still wins, exactly
' as it does for the soft names `warning` and `error` -- this is a fallback
' consulted only when the receiver is not a variable, so it takes nothing.
heartbeat = { tick: "I am a record field" }
append(results, check("a variable shadows the library", "I am a record field", heartbeat.tick))

bad_count = 0
for each v in results
    if not v then
        bad_count += 1
    end if
next v

print ""
print "checks: " + string(count(results))
print "mismatches: " + string(bad_count)
