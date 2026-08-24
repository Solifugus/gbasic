' Catchable now, and the negative index too.
program main( args )
    on error goto next
    a = [1, 2, 3]
    v = a[99]
    if error then
        print "high: " + error.message + " / " + error.source
    end if
    w = a[-1]
    if error then
        print "low: " + error.source
    end if
    print "in range still works: " + string(a[1])
end program
