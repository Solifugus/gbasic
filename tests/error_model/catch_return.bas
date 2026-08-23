' THE case the old model could not do (docs/ai/ERRORS.md proved it):
' a function that catches a raise and returns a clean fallback to a caller
' that never knows anything happened.
function safe_div(a, b)
    on error goto next
    q = a / b
    if error then
        return -1
    end if
    return q
end function

program main( args )
    print safe_div(10, 2)
    print safe_div(10, 0)
    print "still running"
end program
