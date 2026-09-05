' The design draft's §2 minimal example, served for real: routes, a capture,
' a POST body, 404, 405 with Allow, and the static root as the 404 fallback
' (routes win over files on overlap -- the root is consulted only when no
' route answered).
server myapp( port: 0 )

    root "tests/web_server_block/pub"

    get "/"( req )
        return { body: "hello from the block" }
    end get

    get "/products/{id}"( req )
        return { body: "product " + req.params.id }
    end get

    post "/cart"( req )
        return { status: 201, body: "added " + req.body }
    end post

end server

program main( args )
    h = web.serve(myapp)
    print "PORT " + string(h.port)
end program
