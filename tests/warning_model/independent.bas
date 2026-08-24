' An error and a warning can be pending at once and read independently: two
' channels, two flags, no precedence puzzle.
function makes()
    return { a: 1 }
end function

program main( args )
    on error goto next
    on warning goto next
    makes()                  ' warning
    x = 1 / 0                ' error
    if error then
        print "error: " + error.source
    end if
    if warning then
        print "warning still pending and readable: " + warning.source
    end if
end program
