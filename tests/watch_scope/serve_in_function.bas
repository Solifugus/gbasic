' A server held in a function's local, served by that function's watch while
' it waits (docs/watch_scope_design.md §4). Prints PORT, answers `n` requests
' (args[0]) naming how many it has served, then returns and closes.
load webserver

function serve(n)
    server = webserver.listen(0)
    print "PORT " + string(server.port)
    served = 0
    watch(server.requests)
        while count(server.requests) > 0
            req = take_first(server.requests)
            served = served + 1
            append(server.responses, { id: req.id, body: "request " + string(served) + " of " + string(n) })
        end while
    end watch
    wait until served >= n for 30 seconds
    webserver.close(server)
    return served
end function

program main(args)
    print "served " + string(serve(number(args[0])))
end program
