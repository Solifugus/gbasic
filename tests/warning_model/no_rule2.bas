' THE non-interference test. PLAT-ERR rule 2 says a pending ERROR re-raises at
' frame exit. That rule must NOT leak to warnings, or every unchecked warning
' would become an error -- the one thing a warning must never do.
function warns_and_leaves()
    on warning goto next
    makes()                  ' records a warning; deliberately never checked
    return "returned cleanly"
end function

function makes()
    return { a: 1 }
end function

program main( args )
    on error goto next
    r = warns_and_leaves()
    if error then
        print "NOT PRINTED -- an unacknowledged warning must not re-raise"
    end if
    print "got: " + r
end program
