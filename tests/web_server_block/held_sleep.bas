' The CONTROL for the post-serve warning: a HELD listener is bound and
' deliberately never accepted -- PLAT-WEB-2's supervisor hands it to children
' over LISTEN_FDS -- so a supervisor sleeping while it polls those children is
' doing exactly the right thing and must not be warned at.
program main( args )
    load webserver
    server = webserver.listen(0, { hold: true })
    print "held on " + string(server.port)
    sleep(0.05)
    print "slept"
    webserver.close(server)
end program
