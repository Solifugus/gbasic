' Can a SECOND process take the lock the killed one held?
'
' MEASURED, and not what it looks like: this passes with the signal handler
' DELETED, because `flock` is released by the kernel when the process dies.
' So it is an outcome check -- the thing a caller depends on -- and not a
' control on the handler. The handler turned out to do nothing the kernel does
' not already do, and its only observable effect was the exit status.
program main(args)
    f {file}= args[0]
    print "second holder got it: " + string(lock(f))
end program
