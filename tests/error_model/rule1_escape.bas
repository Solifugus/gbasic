' Rule 1: one pending error at a time. The second raise, arriving while the
' first is unacknowledged, is NOT absorbed -- it escapes the frame as if
' unhandled. The deferral privilege is spent until you check.
function double_trouble()
    on error goto next
    a = 1 / 0
    b = 1 / 0
    if error then
        print "checked late"
    end if
    return 0
end function

program main( args )
    on error goto next
    r = double_trouble()
    if error then
        print "escaped to caller: " + error.source
    end if
end program
