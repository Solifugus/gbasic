' Every error carries the call stack at raise time, innermost first. The
' field is `name` (function is a keyword and cannot follow a dot).
function deepest()
    error "boom"
    return 0
end function

function middle()
    return deepest()
end function

program main( args )
    on error goto next
    r = middle()
    if error then
        names = []
        for each fr in error.trace
            append(names, fr.name)
        end for
        print "trace: " + join(names, ",")
    end if
end program
