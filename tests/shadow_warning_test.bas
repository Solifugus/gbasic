' NOT a negative test of failure — a fixture for the read-then-shadow warning.
' It must appear ONCE (deduplicated), name the file, line and column, and the
' program must CONTINUE: it is a warning about a silent no-op, not an error.
function bump()
  total = total + 1
  return total
end function

program main(args)
  total = 5
  print "first:  " + string(bump())
  print "second: " + string(bump())
  print "global: " + string(total)
end program
