' A `server` block asking for client certificates, its TLS settings layered
' on by web.configure (a block's head options are literals; these paths are
' the runner's scratch dir, args[0]). args[1] is "require" or "ask": with
' require_client_cert a client without a certificate is refused in the
' handshake; without it the client may connect and req.tls says it sent none.
' /who answers the subject of the client's certificate, or "none"; over a
' websocket upgraded from it, the message "who" answers the same from c.tls,
' the session net took over from the webserver.
load websocket

server edge( port: 0 )

    get "/who"( req )
        if is_nothing(req.tls.peer_certificate) then
            return { body: "none" }
        end if
        return { body: req.tls.peer_certificate.subject }
    end get

    get "/ws"( req )
        return websocket.accept(req)
    end get

end server

program main( args )
    base = args[0]
    settings = { cert: base + "/server.crt", key: base + "/server.key", client_ca: base + "/ca.crt" }
    if args[1] = "require" then
        settings.require_client_cert = true
    end if
    watch(net.events)
        while count(net.events) > 0
            e = take_first(net.events)
            for each w in websocket.events(e)
                if w.kind = "message" and w.data = "who" then
                    p = w.connection.tls.peer_certificate
                    if is_nothing(p) then
                        websocket.send(w.connection, "none")
                    else
                        websocket.send(w.connection, p.subject)
                    end if
                end if
            end for
        end while
    end watch
    h = web.serve(web.configure(edge, settings))
    print "PORT " + string(h.port)
end program
