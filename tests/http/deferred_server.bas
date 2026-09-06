' THE SHAPE THE WHOLE AI ARCHITECTURE RESTS ON: a handler that does not answer.
'
' A request that needs a model call cannot be answered in the handler -- the
' handler runs on the event loop's thread, so blocking there stalls every other
' client. It must start the call and RETURN, and the answer must be appended
' when the reply arrives, from a different watcher, for a request that arrived
' earlier.
'
' Nothing in the tree had ever done that. It needs no new mechanism: a response
' appended to `server.responses` names its request by id, and the id outlives
' the handler.
load webserver
load http

pending = {}

server = webserver.listen(number(env("PORT")))

watch(server.requests)
    while count(server.requests) > 0
        req = take_first(server.requests)
        h = http.start({ url: "http://127.0.0.1:" + env("UPSTREAM") + "/wait?ms=800" })
        pending[string(h.id)] = req.id
        print "started " + string(req.id)
    end while
end watch

watch(http.events)
    while count(http.events) > 0
        e = take_first(http.events)
        if e.kind = "done" then
            body = http.read(e.handle).body
            rid = pending[string(e.id)]
            append(server.responses, { id: rid, body: "deferred:" + body })
            print "answered " + string(rid)
        end if
    end while
end watch
