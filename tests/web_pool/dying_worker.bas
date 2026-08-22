' Comes up, reports ready, and promptly dies: the probation window's case —
' a replacement that LOOKS healthy for exactly as long as nobody watches it.
program main(args)
  load webserver
  servers = webserver.inherited()
  print "READY sick"
  sleep(0.2)
  error "worker died after ready"
end program
