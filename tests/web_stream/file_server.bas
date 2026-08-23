' A file response and web.static over the same root: both must stream the
' bytes rather than slurping them into a gBASIC string.
program main(args)
  load webserver
  load web
  base = args[0]
  server = webserver.listen(0)
  print "PORT " + string(server.port)
  watch(server.requests)
    while count(server.requests) > 0
      req = take_first(server.requests)
      if req.path = "/direct" then
        h = {}
        h["content-type"] = "application/octet-stream"
        append(server.responses, { id: req.id, file: base + "/blob.bin", headers: h })
      else
        rel = mid(req.path, 1, len(req.path) - 1)
        r = web.static(rel, base)
        r.id = req.id
        append(server.responses, r)
      end if
    end while
  end watch
end program
