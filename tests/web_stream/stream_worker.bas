' A pooled worker holding a PARKED stream: drain (polite TERM) must end the
' stream and let the worker exit itself -- a stream never finishes on its
' own, so draining one means ending it.
program main(args)
  load webserver
  load web
  servers = webserver.inherited()
  server = servers[0]
  print "READY"
  watch(server.requests)
    while count(server.requests) > 0
      req = take_first(server.requests)
      append(server.responses, web.sse(req))
      ' the emit flushes the queued head first, so once the client has read
      ' "data: open" the stream is established end to end -- the driver keys
      ' the TERM on that, not on a timer
      e = webserver.emit(server, req.id, web.sse_event("open"))
    end while
  end watch
end program
