' The first diagnostic the channel carries, and its exemptions. Only a
' gBASIC-defined function returning a non-`nothing` value warns.
function returns_value()
    return { n: 1 }
end function

function returns_nothing()
    return nothing
end function

program main( args )
    on warning goto next

    returns_nothing()
    if warning then
        print "NOT PRINTED -- `return nothing` is the void convention"
    end if

    a = [1]
    append(a, 2)
    if warning then
        print "NOT PRINTED -- builtins are exempt"
    end if

    returns_value()
    if warning then
        print "warned: " + warning.source
    end if

    v = returns_value()
    if warning then
        print "NOT PRINTED -- the result was used"
    end if
    print "used value n=" + string(v.n)
end program
