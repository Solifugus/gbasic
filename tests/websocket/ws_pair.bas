' websocket, BOTH ENDS IN ONE PROGRAM: a watched client talking to this
' program's own `server` block. Self-checking; run by tests/run_websocket.sh,
' and the one fixture here that needs no Python, so it is also the valgrind
' tier's.
'
' The client sends text, binary with every byte value, a 200 000-byte
' message and a ping, then closes with 1000. Both ends must see the same
' conversation, and the program must end once the listener and both
' connections are closed.
load websocket

server pair( port: 0 )

    get "/echo"( req )
        return websocket.accept(req, { protocols: ["p.v1"] })
    end get

end server

function check(label, got, want)
    G.checks = G.checks + 1
    if type(got) = type(want) and string(got) = string(want) then
        print "ok   " + label
    else
        G.mismatches = G.mismatches + 1
        print "MISMATCH " + label + ": got " + string(got) + " (" + type(got) + "), want " + string(want) + " (" + type(want) + ")"
    end if
    return nothing
end function

program main(args)
    G = { checks: 0, mismatches: 0, client: [], server: [], all: "", h: nothing, c: nothing, ended: 0 }
    for i = 0 to 255
        G.all = G.all + pack(">B", [i])
    end for
    watch(net.events)
        while count(net.events) > 0
            e = take_first(net.events)
            for each w in websocket.events(e)
                if w.connection = G.c then
                    on_client(w)
                else
                    on_server(w)
                end if
            end for
        end while
    end watch
    G.h = web.serve(pair)
    G.c = websocket.connect("ws://127.0.0.1:" + string(G.h.port) + "/echo", { protocols: ["p.v1"] })
end program

function on_server(w)
    if w.kind = "open" then
        append(G.server, "open " + w.request.path + " " + w.protocol)
    else if w.kind = "message" then
        append(G.server, "message " + string(byte_count(w.data)))
        if w.binary then
            websocket.send_binary(w.connection, w.data)
        else
            websocket.send(w.connection, w.data)
        end if
    else if w.kind = "ping" then
        append(G.server, "ping " + w.data)
    else if w.kind = "closed" then
        append(G.server, "closed " + string(w.code) + " " + w.reason + " " + string(w.clean))
        ended()
    end if
    return nothing
end function

function on_client(w)
    c = w.connection
    if w.kind = "open" then
        append(G.client, "open " + w.protocol)
        websocket.send(c, "hello")
    else if w.kind = "message" then
        append(G.client, "message " + string(byte_count(w.data)))
        n = count(G.client)
        if n = 2 then
            check("text echoed", w.data, "hello")
            websocket.send_binary(c, G.all)
        else if n = 3 then
            check("every byte value echoed as binary", [w.data = G.all, w.binary], [true, true])
            websocket.send(c, repeat("z", 200000))
        else if n = 4 then
            check("200 000 bytes echoed", w.data = repeat("z", 200000), true)
            websocket.ping(c, "still there")
        end if
    else if w.kind = "pong" then
        append(G.client, "pong " + w.data)
        websocket.close(c, 1000, "bye")
    else if w.kind = "closed" then
        append(G.client, "closed " + string(w.code) + " " + string(w.clean))
        ended()
    end if
    return nothing
end function

function ended()
    G.ended = G.ended + 1
    if G.ended = 2 then
        check("the client's conversation", G.client,
              ["open p.v1", "message 5", "message 256", "message 200000", "pong still there", "closed 1000 true"])
        check("the server's", G.server,
              ["open /echo p.v1", "message 5", "message 256", "message 200000", "ping still there", "closed 1000 bye true"])
        f = webserver.close(G.h)
        print ""
        print "checks: " + string(G.checks)
        print "mismatches: " + string(G.mismatches)
    end if
    return nothing
end function
