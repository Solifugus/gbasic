' Recursion too deep for the stack RAISES (1005, source "recursion") instead of
' crashing the process. run_core.sh runs it: CALLS sets the depth, and the
' program says whether the call returned or was caught, and how.
function down(n)
    if n = 0 then
        return 0
    end if
    return down(n - 1) + 1
end function

on error goto next
x = down(number(env("CALLS")))
e = error
on error stop
if e then
    print "caught code=" + string(e.code) + " source=" + e.source
    print "trace frames=" + string(count(e.trace)) + " first=" + e.trace[0].name + " last=" + e.trace[count(e.trace) - 1].name
else
    print "returned " + string(x)
end if
print "and the program goes on"
