' PLAT-ARRIDX: array ALIASING under copy-on-write.
'
' Arrays are value types backed by a shared, refcounted store
' (docs/array_cow_design.md). Sharing is invisible only for as long as every
' write detaches before touching the store; miss one and the failure is not
' slowness, it is one variable silently changing when another is written.
'
' examples/array_cow_test.bas already pins assignment isolation, nested arrays,
' arrays of records, records of arrays, function arguments and return values.
' This file covers the cases it does NOT, which are the ones where a detach
' happens while another reference to the OLD store is still live:
'
'   * an element assigned FROM THE SAME ARRAY -- the right-hand side is read out
'     of the very store the write is about to detach and free, so a missing copy
'     here reads freed memory rather than returning a wrong answer;
'   * an array mutated DURING iteration, where `for each` holds a snapshot;
'   * an array captured elsewhere and then mutated, in both directions.
'
' It also walks the whole in-place mutator family against a live alias, one line
' each, because every one of them is a separate detach call site in eval.c and a
' new mutator added without a barrier would show up here and nowhere else.

function grab(a)
  return { held: a }
end function

program main(args)
  ' --- 5. an array element assigned from the same array --------------------
  ' The right-hand side is read from the store that the write is about to
  ' detach and free. If the RHS were not copied first, this reads freed memory.
  print "-- element assigned from the same array"
  a = [10, 20, 30]
  b = a                      ' force shared: the write below must detach
  a[0] = a[2]
  print "a=" + json_encode(a) + " b=" + json_encode(b)

  a2 = [1, 2, 3]
  b2 = a2
  a2[1] = a2[1]              ' self-assign, value unchanged
  print "a2=" + json_encode(a2) + " b2=" + json_encode(b2)

  ' nested: element of a nested array assigned from a sibling
  n = [[1, 2], [3, 4]]
  m = n
  n[0][0] = n[1][1]
  print "n=" + json_encode(n) + " m=" + json_encode(m)

  ' --- 6. an array mutated during iteration --------------------------------
  ' `for each` snapshots the container; appending inside the loop must neither
  ' extend the iteration nor corrupt it.
  print "-- mutated during iteration"
  c = [1, 2, 3]
  seen = []
  for each x in c
    append(c, x + 100)
    append(seen, x)
  end for
  print "seen=" + json_encode(seen) + " c=" + json_encode(c)

  ' removing during iteration
  d = [1, 2, 3, 4]
  seen2 = []
  for each x in d
    append(seen2, x)
    d = remove(d, 0)
  end for
  print "seen2=" + json_encode(seen2) + " d=" + json_encode(d)

  ' writing elements during iteration
  e = [1, 2, 3]
  seen3 = []
  for each x in e
    e[0] = 99
    append(seen3, x)
  end for
  print "seen3=" + json_encode(seen3) + " e=" + json_encode(e)

  ' --- 8. an array captured, then mutated after the capture ----------------
  print "-- captured then mutated"
  f = [1, 2, 3]
  cap = grab(f)              ' capture into a record via a function
  append(f, 4)
  print "f=" + json_encode(f) + " captured=" + json_encode(cap.held)

  g = [1, 2]
  holder = [g, g]            ' captured twice into another array
  append(g, 3)
  print "g=" + json_encode(g) + " holder=" + json_encode(holder)

  h = [1, 2]
  rec = { x: h }
  h[0] = 77
  print "h=" + json_encode(h) + " rec.x=" + json_encode(rec.x)

  ' capture, mutate the CAPTURE, original must be untouched
  k = [1, 2]
  rec2 = { x: k }
  rec2.x[0] = 55
  print "k=" + json_encode(k) + " rec2.x=" + json_encode(rec2.x)

  ' --- the mutator family against a live alias -----------------------------
  ' Each in-place mutator must detach before touching the store.
  print "-- mutators with a live alias"
  base = [3, 1, 2]
  al = base
  sort(base)
  print "sort      base=" + json_encode(base) + " alias=" + json_encode(al)

  base = [3, 1, 2]
  al = base
  reverse(base)
  print "reverse   base=" + json_encode(base) + " alias=" + json_encode(al)

  base = [1, 1, 2]
  al = base
  unique(base)
  print "unique    base=" + json_encode(base) + " alias=" + json_encode(al)

  base = [1, 2, 3]
  al = base
  insert(base, 0, 9)
  print "insert    base=" + json_encode(base) + " alias=" + json_encode(al)

  base = [1, 2, 3]
  al = base
  remove(base, 1)
  print "remove    base=" + json_encode(base) + " alias=" + json_encode(al)

  base = [1, 2, 3]
  al = base
  remove_value(base, 2)
  print "rem_value base=" + json_encode(base) + " alias=" + json_encode(al)

  base = [1, 2, 3]
  al = base
  take_first(base)
  print "take_frst base=" + json_encode(base) + " alias=" + json_encode(al)

  base = [1, 2, 3]
  al = base
  take_last(base)
  print "take_last base=" + json_encode(base) + " alias=" + json_encode(al)

  base = [1, 2, 3]
  al = base
  prepend(base, 0)
  print "prepend   base=" + json_encode(base) + " alias=" + json_encode(al)
end program
