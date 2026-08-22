' Single-process soft stop, DRIVEN FROM OUTSIDE: a gBASIC server only serves
' from the event loop after main ends, so the requests come from the runner.
' /last sets server.draining = true; after that the listener must go quiet and
' the PROCESS must exit by itself -- the exit IS the assertion that draining
' releases the event loop.
program main(args)
  load webserver
  server = webserver.listen(0)
  print "PORT " + string(server.port)
  answered = { n: 0 }
  watch(server.requests)
    while count(server.requests) > 0
      req = take_first(server.requests)
      answered.n = answered.n + 1
      append(server.responses, { id: req.id, body: "answer " + string(answered.n) })
      if req.path = "/last" then
        server.draining = true
      end if
    end while
  end watch
end program
