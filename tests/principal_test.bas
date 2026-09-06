' `with principal(p)` and `principal()` -- the identity on whose behalf a body
' acts. Self-checking; run by tests/run_principal.sh.
'
' Step 2 of docs/gbasic_ai_reference_and_primitives.md. The word is the one the
' security literature uses for exactly this, and it is NOT `context`, which
' `reasoning` already defines as {objectives, thresholds, authority, approval}
' -- the same word, no overlap in meaning, and both will be loaded into one
' program the first time an assistant is asked to explain a Finding.
'
' NOT RESERVED. It rides the `with lock(...)` production, whose opener word is
' recognised by POSITION, so `principal` stays an ordinary identifier: a
' variable, a field and a parameter may all be called it. Measured: 0 new
' bison conflicts.
'
' SELF-CHECKING, and here that is forced by the shape of the answer. Every
' defect produces a PLAUSIBLE IDENTITY -- the wrong user, or `nothing` where an
' identity was expected -- and `nothing` prints as an ordinary word that a
' golden would record and defend.

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

' --- NOTHING, never an empty record -----------------------------------------
' "Nobody said" and "acting for a principal that carries no fields" are
' different claims, and the whole point of an enforcement layer is being able
' to refuse the first. Same rule as `materiality` answering `unknown` rather
' than `false` when no threshold was declared.
check("outside any block there is no principal", principal() = nothing, true)
check("and it is not an empty record", principal() = {}, false)

' --- the scope --------------------------------------------------------------
with principal({ user: "alice", groups: ["staff"] })
    check("inside, the principal is the one declared", principal().user, "alice")
    check("and it carries whatever fields it was given",
          principal().groups[0], "staff")
    with principal({ user: "bob" })
        check("a nested block shadows", principal().user, "bob")
    end with
    check("and leaving it restores the outer one", principal().user, "alice")
end with
check("leaving the outermost leaves none", principal() = nothing, true)

' --- dynamic, not lexical ---------------------------------------------------
' The question "who is this being done for" belongs to the CALLER, so a
' function three levels down sees it without any signature carrying it.
function level_three()
    return principal().user
end function

function level_two()
    return level_three()
end function

with principal({ user: "carol" })
    check("a called function sees the caller's principal", level_two(), "carol")
end with
check("and the call did not leave it behind", principal() = nothing, true)

' --- unwinding --------------------------------------------------------------
' A return, a raise and a goto must all leave the scope exactly as falling off
' the end does. A scope that survived a raise would attribute the NEXT action
' to whoever was acting when the last one failed.
function returns_from_inside()
    with principal({ user: "dana" })
        return principal().user
    end with
    return "unreached"
end function

check("a return from inside the block still sees it", returns_from_inside(), "dana")
check("and the block was left", principal() = nothing, true)

function raises_from_inside()
    with principal({ user: "erin" })
        error { message: "raised inside", code: 7 }
    end with
    return "unreached"
end function

on error goto next
ignored = raises_from_inside()
if error then
    check("a raise from inside escapes the block", error.message, "raised inside")
    error.clear()
end if
check("and a raise leaves the scope too", principal() = nothing, true)

function jumps_out()
    with principal({ user: "fay" })
        goto onward
    end with
onward:
    return string(principal() = nothing)
end function

check("a goto out of the block leaves it", jumps_out(), "true")

' --- `principal` is an ordinary word ----------------------------------------
principal = { user: "a variable" }
check("a variable may be called principal", principal.user, "a variable")
rec = { principal: "a field" }
check("so may a field", rec.principal, "a field")

function takes_one(principal)
    return principal
end function
check("so may a parameter", takes_one("a parameter"), "a parameter")

print "checks: " + string(tally.checks)
print "mismatches: " + string(tally.mismatches)
