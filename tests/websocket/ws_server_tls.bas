' websocket, SERVER over TLS: wss:// through a `server` block with a
' certificate. Run by tests/run_websocket.sh, which makes the certificates at
' the literal paths below (a block's head options are literals) and removes
' them afterwards; driven by tests/websocket/ws_peer.py tls.
'
'   gbasic ws_server_tls.bas READY_FILE
'
' The TLS session is the webserver's until the upgrade, then net's: the same
' SSL object, moved with its descriptor. "tls" answers c.tls, so the client
' can see the facts moved with it. "/quit" closes the listener.
load websocket

server secure( port: 0,
               cert: "tests/websocket/.certs/server.pem",
               key: "tests/websocket/.certs/server.key" )

    get "/echo"( req )
        return websocket.accept(req)
    end get

    get "/quit"( req )
        f = webserver.close(G.h)
        return { body: "bye" }
    end get

end server

program main( args )
    G = { h: nothing }
    watch(net.events)
        while count(net.events) > 0
            e = take_first(net.events)
            for each w in websocket.events(e)
                if w.kind = "message" and w.data = "tls" then
                    websocket.send(w.connection, string(w.connection.tls))
                else if w.kind = "message" and w.data = "flood" then
                    ' 100 messages of 100 000 bytes queued at once: the send
                    ' queue grows and moves while libssl holds a refused write
                    for i = 1 to 100
                        websocket.send(w.connection, repeat(chr(64 + mod(i, 26)), 100000))
                    end for
                else if w.kind = "message" then
                    websocket.send(w.connection, w.data)
                end if
            end for
        end while
    end watch
    G.h = web.serve(secure)
    ready {file}= args[0]
    write(ready, string(G.h.port))
end program
