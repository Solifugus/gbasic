program main(args)
  load webserver
  s = webserver.listen(0, { tls: { cert: "/no/such/cert.pem", key: "/no/such/key.pem" } })
  print "must never print"
end program
