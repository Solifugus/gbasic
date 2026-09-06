' A REQUEST HANDLER INHERITS NOTHING, and that is the point.
'
' The listener is created inside a `with principal(...)` block. The handler
' fires from the event loop AFTER `main` has returned, outside every `with`
' block the program ever opened, so `principal()` is `nothing` there -- which
' is correct and is what the architecture requires: a request is acted on for
' whoever SENT it, and that identity comes from the request, never from
' whatever the program happened to be doing when it bound the socket.
'
' THE CONTROL IS THE SAME HANDLER establishing a principal from the request and
' acting under it. Without it, "a handler sees nothing" is satisfied by a
' feature that never works anywhere.
load webserver

with principal({ user: "startup" })
    server = webserver.listen(number(env("PORT")))

    watch(server.requests)
        while count(server.requests) > 0
            req = take_first(server.requests)
            inherited = string(principal() = nothing)
            caller = req.headers["x-user"]
            with principal({ user: caller })
                append(server.responses, {
                    id: req.id,
                    body: "inherited_nothing=" + inherited + " acting_for=" + principal().user
                })
            end with
            print "handled, and after the block: " + string(principal() = nothing)
        end while
    end watch
end with
