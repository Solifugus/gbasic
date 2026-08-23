' The label form: the raise jumps, and firing DISARMS the frame -- shown by
' risky(4) taking the straight path while risky(0) lands in the handler.
function risky(n)
    on error goto handled
    x = 1 / n
    return "ok " + string(x)
handled:
    print "handler ran"
    return "fallback"
end function

program main( args )
    print risky(4)
    print risky(0)
end program
