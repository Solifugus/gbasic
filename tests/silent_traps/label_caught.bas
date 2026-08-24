' Being a raise means it is now CATCHABLE, which the printed line never was.
function f()
    goto nowhere
    return "returned"
end function

program main( args )
    on error goto next
    r = f()
    if error then
        print "caught: " + error.message
        print "source: " + error.source
    end if
    print "still running"
end program
