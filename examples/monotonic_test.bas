' monotonic() -- an elapsed-interval clock (PLAT-DEBT 3).
'
' Every assertion here is a derived FACT, never a measured value: a golden that
' printed a time would differ on every run. The properties are what matter.
'
' Why it exists: `epoch()` is whole seconds of WALL-CLOCK time, so it cannot
' measure a sub-second interval at all, and a duration computed by subtracting
' two wall-clock readings can come out negative when NTP corrects the clock or a
' DST shift lands between them. monotonic() never steps backwards.

program main(args)
  print "-- type and origin"
  t = monotonic()
  print "is_number=" + is_number(t)
  ' The origin is unspecified (in practice, boot), NOT 1970 -- so the value is
  ' far smaller than wall-clock seconds. This is what proves it is a different
  ' clock rather than epoch() with decimals bolted on.
  print "origin_is_not_1970=" + (t < epoch())

  print "-- never goes backwards"
  ' 2000 samples; any single backwards step fails this.
  prev = monotonic()
  ok = true
  n = 0
  while n < 2000
    cur = monotonic()
    if cur < prev then
      ok = false
    end if
    prev = cur
    n = n + 1
  end while
  print "non_decreasing_over_2000_samples=" + ok

  print "-- resolves what epoch() cannot"
  ' Look for an interval that monotonic() measures as greater than zero while
  ' epoch() measures as exactly zero. That is the whole point of the builtin.
  ' Bounded retries so a straddled second boundary cannot make this flaky.
  found = false
  tries = 0
  while tries < 1000
    e0 = epoch()
    m0 = monotonic()
    m1 = monotonic()
    e1 = epoch()
    ' Bound to variables first: `if (` is lexed as the start of a modifier
    ' clause (MOD_LPAREN), so a parenthesised expression cannot open an `if`.
    mdelta = m1 - m0
    edelta = e1 - e0
    if mdelta > 0 then
      if edelta = 0 then
        found = true
        tries = 1000
      end if
    end if
    tries = tries + 1
  end while
  print "sub_second_interval_epoch_reports_as_zero=" + found

  print "-- measures real work"
  ' A loop slow enough to be unambiguous, asserted only as "positive and under a
  ' minute" so the golden cannot depend on machine speed.
  t0 = monotonic()
  s = ""
  i = 0
  while i < 20000
    s = s + "x"
    i = i + 1
  end while
  d = monotonic() - t0
  print "work_len=" + len(s)
  print "elapsed_positive=" + (d > 0)
  print "elapsed_plausible=" + (d < 60)

  print "-- differences are what compose"
  ' Nested intervals: an inner interval cannot exceed the outer one containing it.
  a0 = monotonic()
  b0 = monotonic()
  j = 0
  while j < 5000
    j = j + 1
  end while
  b1 = monotonic()
  a1 = monotonic()
  inner = b1 - b0
  outer = a1 - a0
  print "inner_within_outer=" + (inner <= outer)
end program
