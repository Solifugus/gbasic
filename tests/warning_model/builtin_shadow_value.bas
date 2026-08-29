' A VARIABLE holding a function value, whose name is also a builtin.
'
' The precedence is deliberate -- builtins win, and a function-valued variable
' is the last fallback -- but it was SILENT. `first = my_fn` then `first(xs)`
' quietly ran the BUILTIN and returned an element of xs: no error, a plausible
' value, nothing to see. The library form of this collision has warned for a
' long time; the variable form had nothing.
'
' The warning is claimed here rather than printed, so this fixture asserts WHAT
' fired as well as that something did.

function mine(n)
    return "MINE"
end function

program main( args )
    on warning goto next

    ' 1. THE TRAP. The call reaches the builtin, and the warning says so.
    first = mine
    got = first([9, 8, 7])
    if warning then
    print "warned: " + string(contains(warning.message, "reaches the BUILT-IN"))
    else
    print "warned: false"
    end if
    print "and the builtin answered: " + string(got)

    ' 2. A builtin-named variable holding a NON-function is ordinary and must be
    '    silent -- `list = [3,1,2]` is a perfectly good variable, and only CALLING
    '    it would be the mistake.
    list = [3, 1, 2]
    n = count(list)
    print "non-function variable silent: " + string(not warning) + ", count=" + string(n)

    ' 3. A function value whose name is NOT a builtin is the documented, working
    '    case and must stay quiet.
    handler = mine
    h = handler(1)
    print "non-colliding silent: " + string(not warning) + ", " + h

    ' 4. A RECORD FIELD named like a builtin cannot collide: a field is not a bare
    '    name, so this path is untouched.
    r = { first: mine }
    f = r.first(2)
    print "record field silent: " + string(not warning) + ", " + f
end program
