' While the handler section runs the frame is disarmed: a raise inside it
' propagates to the caller instead of looping back into the handler.
function handler_raises()
    on error goto handled
    x = 1 / 0
    return "unreached"
handled:
    y = 1 / 0
    return "also unreached"
end function

program main( args )
    on error goto next
    r = handler_raises()
    if error then
        print "handler raise propagated: " + error.source
    end if
end program
