' Not another variable either -- gBASIC has no closures, so "which scope" has
' no comfortable answer.
top = 5
function bad(a, b = top)
    return a
end function
