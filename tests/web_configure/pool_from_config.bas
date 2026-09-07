' The end-to-end claim: settings that came from CONFIGURATION, not from
' literals in the source, really reach the runtime.
'
' Everything else about `web.configure` is a fact about a record. This is the
' tier that says the configured value is ACTED ON -- a merge producing a
' perfectly good record the runtime then ignored would pass every other check
' in the suite.
'
' The block declares `port: 0, workers: 1`. Both are overridden, and each
' proves itself in a different way:
'   - the PORT override is proved by the server answering on a port the source
'     never names;
'   - the WORKERS override is proved by the supervisor path running at all,
'     which is observable because only `_serve_pool` prints a PORT line. With
'     the declared `workers: 1` it must NOT appear.
'
' So the same source, run twice with different configuration, must take two
' different paths -- which is what "the override reached the runtime" means.
server app( port: 0, workers: 1 )

    get "/"( req )
        return { body: "pooled answer" }
    end get

end server

program main( args )
    cfg = web.configure(app, {
        port:    number(env("WEB_PORT")),
        workers: number(env("WEB_WORKERS"))
    })
    print to error "declared port " + string(app.options.port) + " workers " + string(app.options.workers)
    print to error "configured port " + string(cfg.options.port) + " workers " + string(cfg.options.workers)
    h = web.serve(cfg)
    print to error "serve returned"
end program
