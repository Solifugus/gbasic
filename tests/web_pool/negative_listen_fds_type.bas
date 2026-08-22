program main(args)
  load webserver
  h = process.start({ command: "true", listen_fds: [42] })
  print "must never print"
end program
