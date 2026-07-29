' PLAT-STRIDX performance fixture. Timed by tests/run_stridx.sh, which asserts
' the SHAPE of the curve (how cost grows with size), never an absolute time --
' absolute times go flaky on a loaded machine.
'
' args: <units> <ascii|multi> <scan|rscan|bytes|len>
'
' Prints one checksum line so a run that silently did nothing cannot pass as a
' fast run. The runner checks it.

function build(n, kind)
  unit = "a"
  if kind = "multi" then
    unit = "e" + chr(233) + chr(8594)
  end if
  c = len(unit)
  s = unit
  while c < n
    s = s + s
    c = c * 2
  end while
  return mid(s, 0, n)
end function

program main(args)
  n = number(args[0])
  kind = args[1]
  op = args[2]

  s = build(n, kind)
  total = 0

  ' Forward per-character scan: the pattern every gBASIC text loop uses.
  if op = "scan" then
    i = 0
    while i < n
      ch = mid(s, i, 1)
      total = total + 1
      i = i + 1
    end while
  end if

  ' Backward per-character scan.
  if op = "rscan" then
    i = n - 1
    while i >= 0
      ch = mid(s, i, 1)
      total = total + 1
      i = i - 1
    end while
  end if

  ' Alternating front/back access -- the worst case for a forward-only cursor,
  ' since every second lookup lands before it.
  if op = "alt" then
    lo = 0
    hi = n - 1
    while lo < hi
      a = mid(s, lo, 1)
      b = mid(s, hi, 1)
      total = total + 2
      lo = lo + 1
      hi = hi - 1
    end while
  end if

  ' Byte access -- O(1) per call by construction, so this isolates the cost of
  ' merely READING the string variable in a loop.
  if op = "bytes" then
    i = 0
    m = byte_count(s)
    while i < m
      b = byte_at(s, i)
      total = total + 1
      i = i + 1
    end while
  end if

  ' The `while i < len(s)` loop shape, which recomputes len every iteration.
  if op = "len" then
    i = 0
    while i < len(s)
      total = total + 1
      i = i + 1
    end while
  end if

  print "checksum " + op + " " + kind + " " + n + " " + total
end program
