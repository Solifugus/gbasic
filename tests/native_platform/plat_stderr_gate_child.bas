' PLAT-STDERR: a gated child for the buffering probe.
'
'   args[0] -- READY file, published only AFTER the first pair of writes has run
'   args[1] -- GATE file, waited on before the second pair
'
' READY is written last on purpose: when the parent sees it, both the stdout and
' the stderr write have provably already executed, so anything absent from the
' pipe at that instant is absent because stdio withheld it -- not because the
' parent looked too early. No clock decides anything here.
program main(args)
    ready = args[0]
    gate = args[1]
    r{file}= ready
    g{file}= gate

    print "OUT-EARLY"
    print to error "ERR-EARLY"

    write(r, "")

    while not exists(g)
        sleep(0.01)
    end while

    print "OUT-LATE"
    print to error "ERR-LATE"
end program
