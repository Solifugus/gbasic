' Same for gosub.
function f()
    gosub nowhere
    return "returned"
end function

program main( args )
    print f()
end program
