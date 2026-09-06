' PLAT-EQ, THE THIRD AND LAST INSTALMENT: the kinds that had no branch at all.
' Self-checking; run by tests/run_equality.sh.
'
' PLAT-EQ (2026-08-14) routed ARRAYS and RECORDS away from eval_comparison's
' numeric fallthrough. The scalar half (2026-09-05) routed STRINGS away from
' it. Neither CLOSED it -- it stayed an open catch-all, and
' `value_number_or_zero` returns 0.0 for every kind that is not a number or a
' boolean, so every kind without a branch of its own was still inside it:
'
'     {file} "/etc/hostname" = {file} "/etc/passwd"   -> TRUE
'     {dir} "/etc" = {dir} "/tmp"                     -> TRUE
'     two separately started child processes          -> TRUE
'     a file = a directory = the number 0             -> TRUE
'     a file > a directory                            -> ANSWERED
'
' `file` and `dir` are ordinary program values, not exotic handles: a program
' comparing two paths was told every pair was equal.
'
' FOUND BY ADDING A THIRTEENTH KIND. PLAT-HTTP's own fixture asserts that two
' transfers are two handles, and it failed -- `http = http` was true. That is
' the argument for closing the door rather than adding a fourteenth branch: a
' new value kind used to opt into the coercion silently, and did.
'
' MEASURED BEFORE FIXING, whole gate instrumented: exactly EIGHT comparisons
' reach the fallthrough with a non-numeric kind (4 http, 3 workbook, 1 actor),
' and every one is an identity comparison the fix answers correctly. Nothing
' depended on the coercion -- which is why it survived three years.
'
' SELF-CHECKING, not golden: every defect here is a plausible BOOLEAN, and a
' golden would have recorded `true` for "two different files are equal".

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

' --- files and directories --------------------------------------------------
a{file}= "/etc/hostname"
b{file}= "/etc/passwd"
a2{file}= "/etc/hostname"
check("two different files are NOT equal", a = b, false)
check("and != says so", a != b, true)
check("a file equals a copy of itself", a = a, true)
check("a file is not the number zero", a = 0, false)
check("a file is not the empty string", a = "", false)

d{dir}= "/etc"
e{dir}= "/tmp"
check("two different directories are NOT equal", d = e, false)
check("a directory equals a copy of itself", d = d, true)
check("a file is not a directory", a = d, false)
check("even when they name the same path", a2 = d, false)

' --- live handles -----------------------------------------------------------
p1 = process.start({ command: "sleep", args: ["30"] })
p2 = process.start({ command: "sleep", args: ["30"] })
alias = p1
check("two separately started children are NOT equal", p1 = p2, false)
check("but a copy of a handle names the same child", alias = p1, true)
check("a child is not the number zero", p1 = 0, false)
check("a child is not a file", p1 = a, false)
stopped1 = process.stop(p1)
stopped2 = process.stop(p2)

' --- THE CONTROL ------------------------------------------------------------
' Without these the fix is indistinguishable from refusing every comparison
' between values of different kinds. number-versus-boolean is a REAL coercion
' with 1,472 measured uses in this tree and is deliberately untouched.
check("0 = false is still true", 0 = false, true)
check("1 = true is still true", 1 = true, true)
check("2 > true is still an answer", 2 > true, true)
check("numbers still compare", 3 > 2, true)
check("strings still compare to strings", "b" > "a", true)
check("and equal strings are still equal", "a" = "a", true)

print "checks: " + string(tally.checks)
print "mismatches: " + string(tally.mismatches)
