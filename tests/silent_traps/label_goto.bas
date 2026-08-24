' A `goto` to a label that does not exist used to print an UNLOCATED line,
' abandon the rest of the function, return `nothing`, and exit 0 -- so a typo'd
' label silently truncated a function and CI saw success. It raises now.
function f()
    goto nowhere
    print "unreachable"
    return "returned"
end function

program main( args )
    print f()
end program
