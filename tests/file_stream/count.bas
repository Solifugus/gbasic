' Walk a file and count its lines, holding nothing. The memory tier runs this
' against two sizes and requires the peak NOT to follow the file.
program main(args)
  f {file}= args[0]
  n = 0
  for each line in f
    n = n + 1
  end for
  print "streamed " + string(n)
end program
