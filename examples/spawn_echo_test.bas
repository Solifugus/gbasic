' Multiprocessing Phase 1c: spawn a worker that replies to its parent
' (docs/multiprocessing_design.md §2-§4). The parent passes its own self()
' handle at spawn; the child receives a message and sends a reply back over the
' inherited handle. Deterministic: a single request/reply.
function worker(parent, name)
    msg = receive()
    send(parent, name + " got: " + msg)
end function

program main(args)
    me = self()
    a = spawn worker(me, "A")
    send(a, "hello")
    print(receive())
end program
