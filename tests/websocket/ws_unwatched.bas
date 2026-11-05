' websocket, REFUSED: an upgrade in a program that watches nothing on
' net.events. Run by tests/run_websocket.sh.
'
'   gbasic ws_unwatched.bas READY_FILE
'
' The upgraded connection would arrive as an `accepted` event nobody reads, so
' the webserver refuses the response (a 500, the reason on stderr) rather
' than handing the socket to nowhere and leaving the client hanging.
load websocket

server lonely( port: 0 )

    get "/ws"( req )
        return websocket.accept(req)
    end get

    get "/quit"( req )
        f = webserver.close(G.h)
        return { body: "bye" }
    end get

end server

program main( args )
    G = { h: nothing }
    G.h = web.serve(lonely)
    ready {file}= args[0]
    write(ready, string(G.h.port))
end program
