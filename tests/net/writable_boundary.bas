' net, WATCHED: `writable` for a queue that reached EXACTLY its 64 KiB mark.
' Run by tests/run_net.sh.
'
'   gbasic writable_boundary.bas SOCKET_PATH
'
' A program that writes "while queued() < 64 KiB" stops at exactly 65536, and
' arming `writable` only ABOVE the mark meant no event ever came. That needs
' the queue to land on the mark, which timing gives only sometimes (it was
' seen once, under the gate's load), so this builds it: the reader holds off,
' the client writes one byte at a time until the kernel refuses one (the
' queue is then exactly 1 byte), then tops the queue up to 65536, and the
' reader drains. The `writable` event must come; a stall is the runner's
' timeout.
load net

program main(args)
    G = { draining: false, srv: nothing, done: false }
    watch(net.events)
        while count(net.events) > 0
            e = take_first(net.events)
            on_event(e)
        end while
    end watch
    G.l = net.listen({ path: args[0] })
    G.c = net.connect({ path: args[0] })
end program

function drain()
    if G.draining and not is_nothing(G.srv) then
        while G.srv.buffered() > 0
            got = G.srv.take()
        end while
    end if
    return nothing
end function

function on_event(e)
    c = e.connection
    if e.kind = "accepted" then
        G.srv = c
        drain()
    end if
    if e.kind = "connected" then
        while c.queued() = 0
            c.write("x")
        end while
        c.write(repeat("y", 65536 - c.queued()))
        print "queued " + string(c.queued())
        G.draining = true
        drain()
    end if
    if e.kind = "data" and c = G.srv then
        drain()
    end if
    if e.kind = "writable" and c = G.c and not G.done then
        G.done = true
        print "writable at " + string(c.queued())
        G.c.close()
        G.srv.close()
        G.l.close()
    end if
    return nothing
end function
