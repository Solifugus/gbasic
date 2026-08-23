' Rule 2 at the top: the program ran to its end, but the pending
' unacknowledged error did not vanish with it.
program main( args )
    on error goto next
    x = 1 / 0
    print "did stuff"
end program
