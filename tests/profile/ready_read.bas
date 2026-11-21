' Says it is ready, then blocks in one read of standard input, which the driver
' holds open: a main thread inside one C call, never at a statement.
program main()
    print "ready"
    x = input("")
    print x
end program
