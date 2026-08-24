' Bare `warning` CLAIMS; `warning.field` does not. Same split as `error`.
function makes()
    return { a: 1 }
end function

program main( args )
    on warning goto next
    makes()
    if warning then
        print "first check true, message present: " + string(len(warning.message) > 0)
    end if
    if warning then
        print "NOT PRINTED"
    else
        print "second check false"
    end if
end program
