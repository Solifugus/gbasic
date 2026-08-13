' PLAT-RECIDX performance fixture. Timed by tests/run_recidx.sh, which asserts
' the SHAPE of the curve (how cost grows with the record's size), never an
' absolute time.
'
' args: <fields> <op>
'
' Two shapes are being pinned, and they are different assertions:
'
'   * Ops that touch every field (build, lookup_all, has_all, keys, foreach)
'     must be LINEAR in the field count -- a 4x size step costs about 4x.
'   * Ops that do a FIXED amount of work against a record that grows
'     (lookup_one, pass, field_dot) must be FLAT -- a 4x size step costs about
'     the same, because neither reading a record variable nor resolving one key
'     may depend on how many fields the record has. These are the ones that were
'     wrong before: 300 000 lookups of a single key cost 0.65 s against a 1 000
'     -field record and 8.41 s against 8 000.
'
' Prints one checksum line so a run that silently did nothing cannot pass as a
' fast run. The runner checks it.

function build(n)
  r = { }
  i = 0
  while i < n
    r["k" + string(i)] = i
    i = i + 1
  end while
  return r
end function

function sink(r)
  return r["k0"]
end function

program main(args)
  n = number(args[0])
  op = args[1]
  total = 0

  ' --- linear ops: work proportional to the field count ---------------------

  ' building a record keyed by string -- the case DOGFOOD measured as O(n^2)
  if op = "build" then
    r = { }
    i = 0
    while i < n
      r["k" + string(i)] = i
      i = i + 1
    end while
    total = count(r)
  end if

  ' reading every key back
  if op = "lookup_all" then
    r = build(n)
    i = 0
    while i < n
      total = total + r["k" + string(i)]
      i = i + 1
    end while
  end if

  ' has() for every key
  if op = "has_all" then
    r = build(n)
    i = 0
    while i < n
      if has(r, "k" + string(i)) then
        total = total + 1
      end if
      i = i + 1
    end while
  end if

  ' keys() materialises the name list
  if op = "keys" then
    r = build(n)
    total = count(keys(r))
  end if

  ' for each over the key list
  if op = "foreach" then
    r = build(n)
    for each k in keys(r)
      total = total + 1
    end for
  end if

  ' overwriting every existing field -- record_set's update path
  if op = "overwrite" then
    r = build(n)
    i = 0
    while i < n
      r["k" + string(i)] = i + 1
      i = i + 1
    end while
    total = r["k0"]
  end if

  ' --- flat ops: fixed work against a record that grows ---------------------

  ' 300 000 lookups of ONE key. Cost must not depend on n.
  if op = "lookup_one" then
    r = build(n)
    j = 0
    while j < 300000
      total = total + r["k0"]
      j = j + 1
    end while
  end if

  ' passing the record to a function 300 000 times -- the value_copy path, which
  ' used to duplicate the whole field array on every read of the variable
  if op = "pass" then
    r = build(n)
    j = 0
    while j < 300000
      total = total + sink(r)
      j = j + 1
    end while
  end if

  ' dot access rather than bracket access, same fixed count
  if op = "field_dot" then
    r = build(n)
    j = 0
    while j < 300000
      total = total + r.k0
      j = j + 1
    end while
  end if

  ' Copying the record into another variable, repeatedly. NOT flat, and
  ' deliberately measured anyway: assigning to a name that already holds an
  ' equal value compares the two records field by field first, to decide whether
  ' a watcher should fire. That comparison is proportional to the field count,
  ' so this op is LINEAR by design and the runner gates it as such. It used to
  ' be quadratic on top of that, because the comparison resolved each name by a
  ' walk -- 5 000 iterations against a 2 000-field record took over ten minutes
  ' before the index and 0.25 s after. A far smaller iteration count than the
  ' flat ops above, for that reason.
  if op = "copy" then
    r = build(n)
    j = 0
    while j < 5000
      c = r
      total = total + 1
      j = j + 1
    end while
  end if

  print "checksum " + op + " " + n + " " + total
end program
