' Both stream shapes plus the drain story, driven by the runner.
'   /tick    emits three events from inside its own handler, then finishes
'   /events  parks; POST /poke broadcasts to every parked stream
'   /        plain, so the runner can see requests still flow beside streams
program main(args)
  load webserver
  load web
  server = webserver.listen(0, { timeout: 1 })
  state = { streams: [] }
  print "PORT " + string(server.port)
  watch(server.requests)
    while count(server.requests) > 0
      req = take_first(server.requests)
      if req.path = "/tick" then
        append(server.responses, web.sse(req))
        n = 0
        ok = true
        while ok and n < 3
          ok = webserver.emit(server, req.id, web.sse_event("tick " + string(n)))
          n = n + 1
        end while
        f = webserver.finish(server, req.id)
        print "tick done emits=" + string(n) + " finish=" + string(f)
      else
        if req.path = "/events" then
          append(server.responses, web.sse(req))
          state.streams = append(state.streams, req.id)
        else
          if req.path = "/poke" then
            kept = []
            for each sid in state.streams
              if webserver.emit(server, sid, web.sse_named("poke", "hello")) then
                kept = append(kept, sid)
              end if
            end for
            state.streams = kept
            append(server.responses, { id: req.id, body: "poked " + string(count(kept)) })
          else
            append(server.responses, { id: req.id, body: "plain" })
          end if
        end if
      end if
    end while
  end watch
end program
