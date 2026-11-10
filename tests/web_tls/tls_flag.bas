' Whether a listener terminates TLS is on its record (`tls`), read from the
' socket's own state (Transward: a program can refuse to start without it).
' args[0] is the runner's scratch dir; args[1] is "tls" or "plain". Prints
' the flag from web.serve's answer and from webserver.listen's, then closes.
load webserver

server edge( port: 0 )
    get "/"( req )
        return { body: "hi" }
    end get
end server

program main( args )
    base = args[0]
    settings = {}
    lopts = {}
    if args[1] = "tls" then
        settings = { cert: base + "/main.crt", key: base + "/main.key" }
        lopts = { tls: settings }
    end if
    h = web.serve(web.configure(edge, settings))
    print "serve " + string(h.tls)
    webserver.close(h)
    s = webserver.listen(0, lopts)
    print "listen " + string(s.tls)
    webserver.close(s)
end program
