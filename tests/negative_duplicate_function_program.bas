' The same, reached through a program block's declaration hoisting rather than
' the top-level walk -- a different registration path, and it must refuse too.
program main()
    print twice(3)
end program

function twice(x)
    return x * 2
end function

function twice(x)
    return x + x
end function
