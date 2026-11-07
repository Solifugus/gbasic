' args[0] is the scratch dir. A misspelled TLS option must be REFUSED by
' name: dropped in silence, `client_cert` for `client_ca` is a server that
' believes it authenticates its clients and does not (DOGFOOD 2026-11-07).
program main(args)
  load webserver
  base = args[0]
  s = webserver.listen(0, { tls: { cert: base + "/main.crt", key: base + "/main.key", client_cert: base + "/ca.crt" } })
  print "must never print"
  ' closed, so a build that accepts the option exits 0 and fails the runner
  ' rather than serving until its timeout
  f = webserver.close(s)
end program
