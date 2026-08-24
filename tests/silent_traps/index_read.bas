' An out-of-range array READ used to print an unlocated line, yield `nothing`
' and exit 0 -- while out-of-range ASSIGNMENT raised properly two lines away in
' the same source file. `nothing` is a legitimate value, so the failure was
' indistinguishable from a real one.
program main( args )
    a = [1, 2, 3]
    v = a[99]
    print "unreachable"
end program
