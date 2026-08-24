' Mode lookup is DYNAMIC, unlike error mode: a caller's `ignore` reaches a
' warning raised inside a callee, because the noise budget belongs to the
' caller. An inner setting still overrides an outer one.
function makes()
    return { a: 1 }
end function

function inner_warns()
    makes()                  ' no setting here -- looks outward
    return nothing
end function

function inner_insists()
    on warning goto next
    makes()
    if warning then
        print "inner override won"
    end if
    return nothing
end function

program main( args )
    on warning ignore
    x = inner_warns()
    print "caller ignore suppressed the callee's warning"
    y = inner_insists()
end program
