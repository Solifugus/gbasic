' Fall-through is to the next statement in the SAME list: inside a loop
' body, that is the next statement of the body, so a per-item failure is
' checked per item and the loop keeps going.
program main( args )
    on error goto next
    total = 0
    entries = [4, 0, 5]
    for each d in entries
        q = 100 / d
        if error then
            print "skip " + string(d)
            continue
        end if
        total = total + q
    end for
    print "total " + string(total)
end program
