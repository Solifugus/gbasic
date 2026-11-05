' websocket, SERVER: an echo server over a `server` block's upgrade route.
' Run by tests/run_websocket.sh, driven by tests/websocket/ws_peer.py.
'
'   gbasic ws_server.bas READY_FILE
'
' GET /echo upgrades (offering the protocol "echo.v1"); every message comes
' back as it came, text as text and binary as binary. A text message "close"
' starts the closing handshake from this side with code 4000. "stats" answers
' what the server has seen. GET /plain is an ordinary route beside it, and
' "/quit" over HTTP closes the listener, and the program ends when its
' WebSockets have closed too. Anything unexpected is printed.
load websocket

server app( port: 0 )

    get "/echo"( req )
        return websocket.accept(req, { protocols: ["echo.v1"] })
    end get

    get "/small"( req )
        return websocket.accept(req, { max_message: 1024 })
    end get

    get "/plain"( req )
        return { body: "plain http still works" }
    end get

    get "/quit"( req )
        G.quit = true
        f = webserver.close(G.h)
        return { body: "bye" }
    end get

end server

program main( args )
    G = { quit: false, opened: 0, messages: 0, pings: 0, closed: [] }
    watch(net.events)
        while count(net.events) > 0
            e = take_first(net.events)
            for each w in websocket.events(e)
                on_ws(w)
            end for
        end while
    end watch
    G.h = web.serve(app)
    ready {file}= args[0]
    write(ready, string(G.h.port))
end program

function on_ws(w)
    c = w.connection
    if w.kind = "open" then
        G.opened = G.opened + 1
    else if w.kind = "message" then
        G.messages = G.messages + 1
        if not w.binary and w.data = "close" then
            websocket.close(c, 4000, "asked to")
        else if not w.binary and w.data = "stats" then
            websocket.send(c, "opened=" + string(G.opened) + " messages=" + string(G.messages) +
                           " pings=" + string(G.pings) + " closed=" + join(G.closed, ","))
        else if not w.binary and w.data = "tls" then
            websocket.send(c, string(c.tls))
        else if w.binary then
            websocket.send_binary(c, w.data)
        else
            websocket.send(c, w.data)
        end if
    else if w.kind = "ping" then
        G.pings = G.pings + 1
    else if w.kind = "closed" then
        append(G.closed, string(w.code))
    else if w.kind != "pong" then
        print "UNEXPECTED " + w.kind + " " + string(w)
    end if
    return nothing
end function
