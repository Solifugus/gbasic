' THE CONTROL on the platform change: adding a delivery route must not disturb
' the blocking one. `receive()` is how a sequential program collects a reply
' and is what `indexer`-shaped programs use; it is unchanged.
function worker(back)
    send(back, "from the worker")
    return nothing
end function

program main(args)
    me = self()
    w = spawn worker(me)
    print receive()
    print "receive still blocks and still returns"
end program
