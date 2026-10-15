function raises_midstream(f)
    for each line in f
        error "failed on: " + line
    end for
    return "never"
end function

function leave_early(f)
    for each line in f
        return "returned from inside the loop: " + line
    end for
    return "never"
end function

program main(args)
  f {file}= args[0]
  write(f, "one" + chr(10) + "two" + chr(10) + "three" + chr(10))

  ' 1. normal end
  n = 0
  for each line in f
    n = n + 1
  end for
  print "normal end        : " + string(n) + " lines"

  ' 2. plain break
  for each line in f
    print "break             : " + line
    break
  end for

  ' 3. break naming an OUTER loop -- travels out of the stream
  for each outer in [1, 2]
    for each line in f
      print "break outer       : " + line
      break outer
    end for
  end for

  ' 4. return from inside the loop, inside a function
  print "return            : " + leave_early(f)

  ' 5. a raise that ESCAPES the loop -- raised in a function with no handler,
  '    caught by this frame. The handle must still be closed on the way out.
  '    (Raising INSIDE a handler-armed loop re-raises on the next iteration and
  '    escapes by design -- PLAT-ERR rule 1 -- and the array form does the same,
  '    measured. That is why the raise is put behind a frame boundary here.)
  on error goto next
  bad = raises_midstream(f)
  if error then
    print "raise escapes     : caught -- " + error.message
    error.clear()
  else
    print "raise escapes     : NOT RAISED (expected one)"
  end if

  ' 6. two streams open at once over the same file
  pairs = 0
  for each a in f
    for each b in f
      pairs = pairs + 1
    end for
  end for
  print "nested streams    : " + string(pairs) + " pairs"

  delete(f)
end program
