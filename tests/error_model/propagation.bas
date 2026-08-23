' Frames start in the default state regardless of the caller's mode: the
' raise in inner() is absorbed by NOBODY until it reaches the armed program
' frame, and the statements after the raise in inner and outer never run --
' the intermediate frames are destroyed, not resumed.
function inner()
    x = 1 / 0
    print "inner after"
    return 0
end function

function outer()
    r = inner()
    print "outer after"
    return r
end function

program main( args )
    on error goto next
    v = outer()
    if error then
        print "caught at top: " + error.source
    end if
    print "alive"
end program
