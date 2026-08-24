' The five typed-value modifiers printed an unlocated line and assigned
' `nothing`, exit 0 -- while `USD`, four lines away in the same dispatch
' function, RAISED. And `number("abc")`, the same strict-conversion shape,
' raises too. These are constructors: failing to construct is an error.
program main( args )
    d(date) = "not-a-date"
    print "unreachable"
end program
