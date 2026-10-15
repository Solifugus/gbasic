' The CONTROL for the memory tier: the whole-file route over the same file. It
' must exceed the gate, or "streaming is flat" is being asserted by a harness
' that cannot tell flat from proportional.
program main(args)
  f {file}= args[0]
  rows = read_lines(f)
  print "read_lines " + string(count(rows))
end program
