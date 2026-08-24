' A library can participate: `warning(...)` -- a BUILTIN CALL, because
' `IDENT expression` as a statement form costs 4 shift/reduce conflicts and the
' whole point of this design is adding no reserved word. Same shape rules as
' `error`: message required, extras become details.
function checked(v)
    if v < 0 then
        warning({ message: "negative input coerced to zero", given: v })
        return 0
    end if
    return v
end function

program main( args )
    on warning goto next
    a = checked(5)
    if warning then
        print "NOT PRINTED"
    end if
    b = checked(-3)
    if warning then
        print "warned: " + warning.message
        print "detail given: " + string(warning.details.given)
    end if
    print "values: " + string(a) + "," + string(b)
end program
