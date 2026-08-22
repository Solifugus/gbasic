program main(args)
  r = process.run({ command: "sh", launch_failure: "maybe" })
  print "must never print"
end program
