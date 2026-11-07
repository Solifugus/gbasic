' args[0] is the scratch dir. require_client_cert with no client_ca has no
' authorities to check a certificate against, so it is refused at listen.
program main(args)
  load webserver
  base = args[0]
  s = webserver.listen(0, { tls: { cert: base + "/main.crt", key: base + "/main.key", require_client_cert: true } })
  print "must never print"
end program
