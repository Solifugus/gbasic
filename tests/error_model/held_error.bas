' A frame keeps the error it absorbed (DOGFOOD 2026-11-27). There is one
' current error, and a callee that catches a raise of its own replaces it; a
' frame still holding its own unchecked error must read, re-raise and report
' THAT one. Before the fix each of the three below named the callee's
' division by zero, which was handled, instead of this frame's index.
function inner(x)
    on error goto next
    y = 1 / x
    if error then
        return "inner handled " + error.message
    end if
    return "ok"
end function

' a check after the callee returns reads this frame's error
function reads()
    on error goto next
    z = [1][5]
    print inner(0)
    if error then
        return "reads saw " + error.message + " at line " + string(error.line)
    end if
    return "nothing"
end function

' returning unchecked re-raises this frame's error, not the callee's
function leaves()
    on error goto next
    z = [1][5]
    print inner(0)
    return "unreached"
end function

print reads()
on error goto next
print leaves()
if error then
    print "caller caught: " + error.message + " at line " + string(error.line)
end if
' a snapshot taken after a callee handled its own is still this frame's
function snaps()
    on error goto next
    z = [1][7]
    print inner(0)
    e = error
    return e.line
end function
print "snapshot line " + string(snaps())
' and a frame whose error WAS checked reads the latest one, as before
function checked()
    on error goto next
    z = [1][5]
    if error then
        print "checked here"
    end if
    print inner(0)
    return error.message
end function
print "after a check: " + checked()
