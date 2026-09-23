' Takes a lock and waits to be killed. Prints BEFORE waiting, so the driver
' signals it at a moment when the lock is provably held.
program main(args)
    f {file}= args[0]
    write(f, "x")
    got = lock(f)
    print "holding " + string(got)
    sleep(30)
end program
