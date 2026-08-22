program main(args)
  load webserver
  s = webserver.listen(0, { hold: "yes" })
  print "must never print"
end program
