' The declaration is position-blind: the block sits BELOW the program block
' that reads it, exactly like a function -- the pre-registration contract
' (tests/run_pre_registration.sh) extended to servers.
program main( args )
    print "sees " + below.name + " with " + string(count(below.sites[0].routes)) + " route"
    h = below.sites[0].routes[0].handler
    r = h({ id: 1, method: "GET", path: "/" })
    print "handler answers: " + r.body
end program

server below( port: 0 )
    get "/"( req )
        return { body: "declared late, bound early" }
    end get
end server
