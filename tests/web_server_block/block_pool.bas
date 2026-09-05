' `workers: 2` -- the supervisor path. web.serve() binds with hold:, spawns two
' copies of THIS PROGRAM via process.self() over the §7 pool, and blocks
' ticking until asked to stop. A spawned copy runs this same source, finds
' inherited sockets in web.serve(), adopts them, prints READY and serves.
server farm( port: 0, workers: 2 )

    get "/"( req )
        return { body: "pooled answer" }
    end get

end server

program main( args )
    h = web.serve(farm)
    print to error "supervisor done"
end program
