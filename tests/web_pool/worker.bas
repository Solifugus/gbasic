' The canonical pool worker: adopt the inherited listener, announce readiness,
' answer requests. Answers carry the VERSION marker from argv so a rolling
' reload can prove which code is serving. A /slow request blocks on a gate
' file, which is how the drain tests hold a request in flight deterministically.
program main(args)
  load webserver
  version = args[0]
  gatepath = args[1]
  servers = webserver.inherited()
  if count(servers) = 0 then
    print to error "worker: nothing inherited"
    error "no inherited listener"
  end if
  server = servers[0]
  ' the protocol variables must be CONSUMED by adoption, or a worker's own
  ' children would mistake them for their own inheritance
  if not is_unknown(env("LISTEN_FDS")) then
    print to error "worker: LISTEN_FDS survived adoption"
  end if
  print "READY " + version
  watch(server.requests)
    while count(server.requests) > 0
      req = take_first(server.requests)
      if req.path = "/slow" then
        print to error "in-handler /slow"
        gate{file} = gatepath
        while not exists(gate)
          sleep(0.05)
        end while
        append(server.responses, { id: req.id, body: "slow " + version })
      else
        append(server.responses, { id: req.id, body: "hello " + version })
      end if
    end while
  end watch
end program
