' PLAT-STREAM child fixture: print, publish READY, then never terminate on its
' own. The parent kills it; whatever left the process before the signal is all a
' consumer can possibly keep, because a signalled process runs no stdio cleanup.
program main(args)
    ready{file} = args[0]

    print "BEFORE-SIGNAL"
    write(ready, "")

    while true
        sleep(0.05)
    end while
end program
