' The four modes. `unused` is the first diagnostic the channel carries: a bare
' call discarding a non-`nothing` return from a gBASIC function.
function makes(n)
    return { n: n }
end function

function under_ignore()
    on warning ignore
    makes(1)
    return nothing
end function

function under_print()
    on warning print
    makes(2)
    return nothing
end function

function under_next()
    on warning goto next
    makes(3)
    if warning then
        print "recorded: " + warning.source
    end if
    return nothing
end function

program main( args )
    x = under_ignore()
    y = under_print()
    z = under_next()
    print "all three modes ran"
end program
