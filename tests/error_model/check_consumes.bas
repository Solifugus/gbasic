' Bare `error` answers "is there an UNACKNOWLEDGED error?" and claims it:
' true exactly once per raise, false on a later check, false after success.
' `error.field` reads the stored error without touching the flag, which is
' what lets the block body describe what it caught.
program main( args )
    on error goto next
    x = 1 / 0
    if error then
        print "caught: " + error.message
        print "source: " + error.source
    end if
    if error then
        print "stale true"
    else
        print "second check false"
    end if
    y = 2 + 2
    if error then
        print "false positive"
    else
        print "success leaves no error"
    end if
end program
