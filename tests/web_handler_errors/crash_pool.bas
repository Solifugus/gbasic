' The crossing: a handler that RAISES, under `workers: 2`.
'
' A raise inside a handler is let-it-crash by design (§7b) -- the worker dies
' and its supervisor restarts it. Every other fixture runs at the default
' `workers: 1`, where serve() never becomes a supervisor and there is nothing
' to do the restarting, so the design's own error model has never actually
' been exercised. This is that test: kill a worker through the front door and
' require the service to come back.
server crashy( port: 0, workers: 2 )

    get "/ok"( req )
        return { body: "fine" }
    end get

    get "/crash"( req )
        error "handler exploded"
        return { body: "unreachable" }
    end get

end server

program main( args )
    h = serve(crashy)
    print to error "supervisor done"
end program
