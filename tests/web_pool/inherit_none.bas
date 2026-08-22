' Run OUTSIDE any supervisor: nothing inherited is the normal answer, an empty
' array, never an error -- the local-development fallback path.
program main(args)
  load webserver
  servers = webserver.inherited()
  print "inherited: " + string(count(servers))
  print "type: " + type(servers)
end program
