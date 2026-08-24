' `w = warning` acknowledges and snapshots in one move.
function makes()
    return { a: 1 }
end function

program main( args )
    on warning goto next
    makes()
    w = warning
    if w then
        print "snapshot source: " + w.source
    end if
    if warning then
        print "NOT PRINTED -- the snapshot claimed it"
    else
        print "claimed by the snapshot"
    end if
end program
