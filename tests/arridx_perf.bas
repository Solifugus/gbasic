' PLAT-ARRIDX performance fixture. Timed by tests/run_arridx.sh, which asserts
' the SHAPE of the curve (how cost grows with size), never an absolute time.
'
' args: <elements> <op>
'
' Prints one checksum line so a run that silently did nothing cannot pass as a
' fast run. The runner checks it.

function build(n)
  a = []
  i = 0
  while i < n
    append(a, i)
    i = i + 1
  end while
  return a
end function

function sink(a)
  return count(a)
end function

program main(args)
  n = number(args[0])
  op = args[1]
  total = 0

  ' arr[i] inside a while loop -- the pattern UNLEARN.md still warns about.
  if op = "index" then
    a = build(n)
    i = 0
    while i < n
      total = total + a[i]
      i = i + 1
    end while
  end if

  ' the same read via for each, the documented workaround
  if op = "foreach" then
    a = build(n)
    for each x in a
      total = total + x
    end for
  end if

  ' bare append statement -- 669 of ~690 call sites in the tree
  if op = "append" then
    a = []
    i = 0
    while i < n
      append(a, i)
      i = i + 1
    end while
    total = count(a)
  end if

  ' append used as an EXPRESSION, which must produce a value
  if op = "append_expr" then
    a = []
    i = 0
    while i < n
      a = append(a, i)
      i = i + 1
    end while
    total = count(a)
  end if

  ' count(a) re-evaluated every iteration
  if op = "count" then
    a = build(n)
    i = 0
    while i < count(a)
      total = total + 1
      i = i + 1
    end while
  end if

  ' passing a large array to a function, repeatedly
  if op = "pass" then
    a = build(n)
    i = 0
    while i < 200
      total = total + sink(a)
      i = i + 1
    end while
  end if

  ' element WRITE in a loop -- exercises the COW detach path
  if op = "write" then
    a = build(n)
    i = 0
    while i < n
      a[i] = i + 1
      i = i + 1
    end while
    total = a[n - 1]
  end if

  ' write to an array that is genuinely shared, forcing a detach each time
  if op = "write_shared" then
    a = build(n)
    i = 0
    while i < 200
      b = a
      b[0] = i
      total = total + b[0]
      i = i + 1
    end while
  end if

  ' arrays of RECORDS -- what Studio actually holds
  if op = "records" then
    a = []
    i = 0
    while i < n
      append(a, { id: i, name: "sec", kind: "statements" })
      i = i + 1
    end while
    i = 0
    while i < n
      total = total + a[i].id
      i = i + 1
    end while
  end if

  print "checksum " + op + " " + n + " " + total
end program
