' error.clear() clears the stored error AND the pending flag -- rarely
' needed now that the check itself acknowledges, but it must keep working.
program main( args )
    on error goto next
    x = 1 / 0
    error.clear()
    if error then
        print "still pending"
    else
        print "cleared"
    end if
    print "message now: [" + error.message + "]"
end program
