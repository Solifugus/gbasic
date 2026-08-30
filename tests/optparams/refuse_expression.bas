' Not an arbitrary expression: it would need a scope to be evaluated in.
function bad(a, b = 1 + 1)
    return a
end function
