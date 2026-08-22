function two(a, b)
  return a + b
end function

program main(args)
  x = two(1, 2, 3)
  print "must never print: the call above is the error, located, and fatal"
end program
