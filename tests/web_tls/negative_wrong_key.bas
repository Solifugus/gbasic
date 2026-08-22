' args[0] is the scratch dir: main.crt paired with alt.key must be REFUSED at
' listen time — a server that comes up with a mismatched pair fails at the
' first handshake instead, hours later and per-client.
program main(args)
  load webserver
  base = args[0]
  s = webserver.listen(0, { tls: { cert: base + "/main.crt", key: base + "/alt.key" } })
  print "must never print"
end program
