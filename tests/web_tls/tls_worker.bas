' The pooled TLS worker: a plain TCP socket arrives by inheritance; TLS is
' terminated HERE, in the worker, from certs the worker loads at start —
' which is what makes certificate rotation a rolling reload and nothing more.
program main(args)
  load webserver
  base = args[0]
  servers = webserver.inherited({ tls: { cert: base + "/main.crt", key: base + "/main.key" } })
  if count(servers) = 0 then
    error "nothing inherited"
  end if
  server = servers[0]
  print "READY"
  watch(server.requests)
    while count(server.requests) > 0
      req = take_first(server.requests)
      append(server.responses, { id: req.id, body: "pooled tls: " + req.scheme })
    end while
  end watch
end program
