' PLAT-STREAM child fixture: print, publish a READY file, block on a GATE file,
' print again, exit.
'
' READY is created AFTER the first `print` statement has executed, so a parent
' that waits for READY knows the print ran. Whether its bytes have left the
' process by then is therefore purely a question of stdout buffering -- never of
' timing, no matter how slow the host (or valgrind) is.
'
' Receiving READY/GATE as program arguments also proves the interpreter consumed
' --line-buffered itself: if the flag leaked through to the program, args[0]
' would be the flag rather than the READY path and this fixture would misbehave.
program main(args)
    ready(file) = args[0]
    gate(file) = args[1]

    print "CHUNK-ONE"
    write(ready, "")

    while not exists(gate)
        sleep(0.01)
    end while

    print "CHUNK-TWO"
end program
