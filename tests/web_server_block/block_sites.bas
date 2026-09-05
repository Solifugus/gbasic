' Host dispatch across sites -- and the UNBOUND serve: the live server record
' is bound only in boot()'s local frame, which is gone by the time requests
' arrive. Dispatch must not depend on a global binding, because the draft's
' own example calls web.serve(edge) without keeping the result at all.
server edge( port: 0 )

    get "/"( req )
        return { body: "default site" }
    end get

    web store( host: "store.example" )
        get "/"( req )
            return { body: "store site" }
        end get
        get "/only-store"( req )
            return { body: "store only" }
        end get
    end web

    web api( host: "api.example" )
        get "/"( req )
            return { body: "api site" }
        end get
    end web

end server

function boot()
    s = web.serve(edge)
    print "PORT " + string(s.port)
    return 0
end function

program main( args )
    r = boot()
end program
