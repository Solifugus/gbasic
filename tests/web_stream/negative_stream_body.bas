program main(args)
  load webserver
  server = webserver.listen(0)
  r = { id: 1, stream: true, body: "no" }
  append(server.responses, r)
  server.running = false
end program
