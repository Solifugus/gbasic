' A TLS listener with a default cert and one SNI host. Certs come from the
' scratch dir the runner generated them into (args[0]).
program main(args)
  load webserver
  base = args[0]
  server = webserver.listen(0, { tls: {
      cert: base + "/main.crt", key: base + "/main.key",
      certs: [ { host: "alt.example", cert: base + "/alt.crt", key: base + "/alt.key" } ] } })
  print "PORT " + string(server.port)
  watch(server.requests)
    while count(server.requests) > 0
      req = take_first(server.requests)
      append(server.responses, { id: req.id, body: "secure hello via " + req.scheme })
    end while
  end watch
end program
