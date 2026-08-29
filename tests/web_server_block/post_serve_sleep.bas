' PLAT-WEB: the post-serve loop trap.
'
' A single-process server serves from the event loop that runs AFTER main
' returns, so the shape every service author reaches for --
'
'     h = serve(app)
'     while h.running
'         sleep(0.25)
'     end while
'
' -- never reaches it. The listener binds, the banner prints, `h.running` is
' true and the port ACCEPTS CONNECTIONS, so every external check short of an
' actual request says the service is healthy while every request hangs forever
' with no response and nothing on stderr. Reported by the Transward build,
' which lost the diagnosis twice before reducing it to four lines.
server app( port: 0 )
    get "/"( req )
        return { body: "hello" }
    end get
end server

program main( args )
    h = serve(app)
    print "bound"
    sleep(0.05)
    print "slept"
end program
