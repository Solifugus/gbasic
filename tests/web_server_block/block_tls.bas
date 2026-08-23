' TLS from the head: cert/key on the server itself become the default pair.
' The paths are literals (head options always are), so the runner generates
' the pair at this fixed location before running this.
server sec( port: 0,
            cert: "tests/web_server_block/.certs/main.crt",
            key: "tests/web_server_block/.certs/main.key" )

    get "/"( req )
        return { body: "secure " + req.scheme }
    end get

end server

program main( args )
    h = serve(sec)
    print "PORT " + string(h.port)
end program
