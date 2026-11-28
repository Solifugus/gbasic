' A VARIABLE holding a function value, whose name is also a builtin's or a
' function's.
'
' Until 2026-11-27 builtins won, then the program's functions, and the
' variable was the last fallback: `first = my_fn` then `first(xs)` ran the
' BUILTIN (warned, 2102, since 2026-08-28), and a parameter `f` lost `f()` to
' the program's own `f` with no word at all. Matthew's ruling: the variable
' wins. A function value gets into a variable only on purpose, and it is the
' nearest binding. So nothing warns, and this fixture asserts the value ran.

function mine(n)
    return "MINE"
end function

function other()
    return "OTHER"
end function

' a parameter named after a function this file defines
function call_it(mine)
    return mine()
end function

program main( args )
    on warning goto next

    ' 1. A builtin's name: the value is called, and nothing warns.
    first = mine
    got = first([9, 8, 7])
    print "warned: " + string(warning)
    print "and the value answered: " + string(got)

    ' 1b. A function's name, through a parameter: the value passed is called.
    print "a parameter's value: " + call_it(other)

    ' 2. A builtin-named variable holding a NON-function changes nothing: the
    '    call is the builtin's, silently.
    list = [3, 1, 2]
    n = count(list)
    print "non-function variable silent: " + string(not warning) + ", count=" + string(n)

    ' 3. A function value whose name is nobody else's must stay quiet.
    handler = mine
    h = handler(1)
    print "non-colliding silent: " + string(not warning) + ", " + h

    ' 4. A RECORD FIELD named like a builtin cannot collide: a field is not a bare
    '    name, so this path is untouched.
    r = { first: mine }
    f = r.first(2)
    print "record field silent: " + string(not warning) + ", " + f
end program
