' A raise inside an ARGUMENT abandons the call: the callee is never entered.
'
' docs/error_model_design.md §2.1 says a raise under `on error goto next`
' abandons the raising statement, and §2 that "the call that was in flight is
' abandoned". Until 2026-08-29 that held for a BUILTIN callee and not for a
' gBASIC one -- eval_user_function_with_receiver evaluated the remaining
' arguments and ran the body anyway, so a function executed on arguments that
' never successfully evaluated. It surfaced as a phantom `nothing` on STDOUT,
' because with an error pending the calls inside the body short-circuit while
' literals do not, so `print "a=" + string(a)` printed just `nothing`.
'
' Lines say ok or WRONG rather than merely existing, so a regression names
' itself instead of quietly moving a golden. NOTE the WRONG markers are only
' half the detection here, and the weaker half: a body that runs while an error
' is pending has its own `print` poisoned, so a marker built by concatenation
' comes out as the bare word `nothing` rather than as WRONG. Verified against a
' binary with the guard disabled -- the nested case, whose marker is a plain
' literal, printed WRONG, while the two built by concatenation printed
' `nothing`. The golden diff is what catches both.

' Never legitimately called: every use below must be abandoned.
function must_not_run(a, b)
    print "WRONG: the body ran with a=" + string(a) + " b=" + string(b)
    return "ret"
end function

function nested_must_not_run(x)
    print "WRONG: the nested body ran"
    return x
end function

' The control's callee, which must run normally.
function healthy(a, b)
    print "ok   a healthy call still runs, a=" + a + " b=" + b
    return "ret"
end function

' `join_path` takes exactly two arguments, so three raises.
on error goto next

' ---------------------------------------------------- bare call statement
must_not_run("x", join_path("a", "b", "c"))
if error then
    print "ok   bare call: abandoned, and the raise is catchable"
    print "     message: " + error.message
else
    print "WRONG: no error after a failed argument"
end if
error.clear()

' ------------------------------------------------------- assignment form
' Seeded first, so the check below proves the assignment did not HAPPEN rather
' than merely that `r` holds nothing. Reading an unassigned `r` would itself
' raise, which -- with the first error still pending -- escapes the frame under
' rule 1 and ends the program, so the sentinel is load-bearing, not decoration.
r = "SENTINEL"
r = must_not_run("x", join_path("a", "b", "c"))
if error then
    print "ok   assignment: abandoned"
else
    print "WRONG: assignment form did not raise"
end if
' Clear BEFORE inspecting `r`: while an error is pending every call in the
' expression short-circuits, so `is_unknown(r)` would answer nothing and the
' check would silently test neither branch.
error.clear()
if r = "SENTINEL" then
    print "ok   and the assignment target was left untouched"
else
    print "WRONG: the target got " + string(r)
end if

' ------------------------------------------------------------ nested call
n = nested_must_not_run(join_path("a", "b", "c"))
if error then
    print "ok   nested: abandoned"
else
    print "WRONG: nested form did not raise"
end if
error.clear()

' Arguments LEFT of the raise are still evaluated -- abandonment stops AT the
' failure, it does not make the argument list atomic. Pinned because it is the
' part a reader would otherwise assume either way.
function note(tag)
    print "     evaluated " + tag
    return tag
end function

must_not_run(note("first"), join_path("a", "b", "c"))
if error then
    print "ok   an argument before the failure still evaluated"
else
    print "WRONG: no error"
end if
error.clear()

' ----------------------------------------------------- via a function VALUE
' A distinct user-facing shape, and confirmed to share the same guard: with
' the guard disabled this call ran the body too.
fv = must_not_run
r2 = fv("x", join_path("a", "b", "c"))
if error then
    print "ok   through a function value: abandoned"
else
    print "WRONG: the function-value form did not raise"
end if
error.clear()

on error stop

' ------------------------------------------------- CONTROL: a healthy call
' Without this the tier is satisfied by a build that never calls anything.
good = healthy("x", join_path("a", "b"))
print "ok   and returned " + good
