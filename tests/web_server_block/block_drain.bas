' A block server for the drain driver: polite TERM must run the on drain
' hook and let the process exit itself with code 0.
server app( port: 0 )

    get "/"( req )
        return { body: "alive" }
    end get

    on drain
        print to error "hook: draining now"
    end on

end server

program main( args )
    h = serve(app)
    print "PORT " + string(h.port)
end program
