' PLAT-STREAM child fixture: emit args[0] numbered lines as fast as it can.
' Used to check that promptly-flushed output is still COMPLETE and byte-exact,
' and to keep the flag's throughput cost honest.
program main(args)
    n = number(args[0])
    i = 0
    while i < n
        print "line " + i
        i = i + 1
    end while
end program
