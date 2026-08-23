' Rule 2: pending errors do not survive the frame. Returning with an
' unacknowledged error converts the return into a re-raise at the call
' site; the returned value is discarded.
function sloppy()
    on error goto next
    x = 1 / 0
    return 42
end function

program main( args )
    on error goto next
    v = sloppy()
    if error then
        print "re-raised: " + error.message
    else
        print "no error"
    end if
end program
