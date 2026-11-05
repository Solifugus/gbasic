' net, DATAGRAMS (N3), WATCHED: a UDP echo server driven by tests/net/udp_peer.py.
' Run by tests/run_net.sh.
'
'   gbasic datagram_server.bas READY_FILE
'
' Two sockets, their ports written to READY_FILE as "echo small": `echo`
' answers each datagram with itself, to the address it came from; `small` has
' max_datagram 16 and answers with what it was told about the datagram. A
' datagram "quit" closes both, and the program ends when their closed events
' have been delivered. Anything unexpected is printed.
load net

program main(args)
    G = { closed: 0 }
    watch(net.events)
        while count(net.events) > 0
            e = take_first(net.events)
            on_event(e)
        end while
    end watch
    G.echo = net.datagram({ port: 0 })
    G.small = net.datagram({ port: 0, max_datagram: 16 })
    ready {file}= args[0]
    write(ready, string(G.echo.port) + " " + string(G.small.port))
end program

function on_event(e)
    if e.kind = "datagram" then
        if e.data = "quit" then
            G.echo.close()
            G.small.close()
        else if e.socket = G.small then
            e.socket.send_to("truncated:" + string(e.truncated) + " size:" + string(e.size) + " kept:" + e.data, e.from)
        else
            e.socket.send_to(e.data, e.from)
        end if
    else if e.kind = "closed" then
        G.closed = G.closed + 1
        if e.code != "local" then
            print "UNEXPECTED closed " + e.code
        end if
    else
        print "UNEXPECTED " + e.kind + " " + string(e.code) + " " + string(e.message)
    end if
    return nothing
end function
