' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' THE SHAPE THE ASK ASKED FOR, end to end (gdash11_platform_ask_stream.md): a
' parked SSE stream poked ON A SCHEDULE, with no handler holding a worker.
' Before `timer` this could not be written at all -- parking worked, and
' nothing existed to do the poking.
server app( port: 0 )
    stream "/events"( req )
        e = web.emit(req, web.sse_event("open"))
        append(G.streams, req)
        return 0
    end stream

    get "/ping"( req )
        return { body: "pong" }
    end get
end server

program main( args )
    G = { streams: [] }
    h = web.serve(app)
    print "PORT " + string(h.port)
    t = timer.every(0.2)
    watch(timer.ticks)
        while count(timer.ticks) > 0
            ev = take_first(timer.ticks)
            alive = []
            for each s in G.streams
                if web.emit(s, web.sse_named("tick", string(ev.count))) then
                    append(alive, s)
                end if
            end for
            G.streams = alive
        end while
    end watch
end program
