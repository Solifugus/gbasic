' Streams in the block: an in-handler emit loop, and the parked shape poked
' from another request's handler. Handlers are functions and cannot rebind
' caller state, so shared state lives in FIELDS of a program global -- the
' one mutation the language lets a function make visible (the same pattern
' Studio's shell uses).
server sse( port: 0 )

    stream "/tick"( req )
        n = 0
        while n < 3
            e = web.emit(req, web.sse_event("tick " + string(n)))
            n = n + 1
        end while
        f = web.finish(req)
        return 0
    end stream

    stream "/events"( req )
        e = web.emit(req, web.sse_event("open"))
        append(G.streams, req)
        return 0
    end stream

    post "/poke"( req )
        kept = []
        for each s in G.streams
            ok = web.emit(s, web.sse_named("poke", "hello"))
            if ok then
                append(kept, s)
            end if
        end for
        G.streams = kept
        return { body: "poked " + string(count(kept)) }
    end post

end server

program main( args )
    G = { streams: [] }
    h = web.serve(sse)
    print "PORT " + string(h.port)
end program
