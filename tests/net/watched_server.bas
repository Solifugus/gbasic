' net, WATCHED: a server the event loop drives. Run by tests/run_net.sh, with
' tests/net/peer.py as the other end -- a client this module did not write.
'
'   gbasic watched_server.bas MODE READY_FILE
'
' Writes the port to READY_FILE once listening. Modes:
'   line   echo each line back as "echo:" + line; "quit" closes the server
'   frame  echo each length-prefixed frame (>I length, then the bytes)
'   flood  on accept, write 10 MiB of a known pattern, then close
'   hold   never take; a 64 KiB buffer, so the `full` event must come
load net

program main(args)
    G = { mode: args[0], events: [] }
    watch(net.events)
        while count(net.events) > 0
            e = take_first(net.events)
            on_event(e)
        end while
    end watch
    buffer = 16777216
    if G.mode = "hold" then
        buffer = 65536
    end if
    G.server = net.listen({ port: 0, buffer: buffer, max_connections: 2000 })
    ready {file}= args[1]
    write(ready, string(G.server.port))
end program

function on_event(e)
    c = e.connection
    if e.kind = "accepted" and G.mode = "flood" then
        c.write(repeat("0123456789abcdef", 655360))
        c.close()
        return nothing
    end if
    if e.kind = "full" then
        print "full"
        return nothing
    end if
    if e.kind = "data" and G.mode = "line" then
        line = c.take_line()
        while not is_unknown(line)
            if line = "quit" then
                c.close()
                G.server.close()
                return nothing
            end if
            c.write("echo:" + line + chr(10))
            line = c.take_line()
        end while
    end if
    if e.kind = "data" and G.mode = "frame" then
        head = c.peek_exactly(4)
        while not is_unknown(head)
            n = unpack(">I", head)[0]
            if c.buffered() < 4 + n then
                return nothing
            end if
            frame = c.take_exactly(4 + n)
            c.write(frame)
            head = c.peek_exactly(4)
        end while
    end if
    return nothing
end function
