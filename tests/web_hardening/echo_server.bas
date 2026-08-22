' A server that never answers: the 504/408 cases need exactly that. timeout: 1
' keeps the suite fast; the option under test IS the reason it can be fast.
program main(args)
  load webserver
  server = webserver.listen(0, { timeout: 1 })
  print "PORT " + string(server.port)
end program
