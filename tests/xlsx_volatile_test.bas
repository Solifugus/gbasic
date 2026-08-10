' NOW() / TODAY() -- the Excel date serial, under a PINNED clock.
'
' Driven by tests/run_xlsx.sh, not run_examples.sh, because it is only
' deterministic with GBASIC_XLSX_NOW set. The runner supplies both that and TZ;
' run bare, this prints the real clock and is meant to.
'
' WHAT IS ACTUALLY BEING TESTED. Excel's epoch is 1899-12-30, not 1900-01-01,
' because Lotus 1-2-3 treated 1900 as a leap year and Excel reproduced the bug
' for file compatibility. That two-day shift is exactly the kind of arithmetic
' that is wrong by one and stays wrong, and a test asserting only "NOW is a
' plausible number" would never catch it. Hence the pinned clock.
'
' The serials asserted here were cross-checked against LibreOffice -- an
' independent implementation of the same broken epoch -- not merely derived
' from the same reasoning as the code.
'
' Note the shape of the assertions: the DAY and the SECONDS-OF-DAY are checked
' separately rather than printing the raw serial. gBASIC's `print` renders
' numbers with about six significant digits, so a full serial like
' 46237.5674884 displays as 46237.6 and a golden over it would pin almost
' nothing. See the 2026-08-09 DOGFOOD entry.

program main(args)
  wb = xlsx.open("examples/fixtures/xlsx/volatile.xlsx")

  n = xlsx.evaluate(wb, "Volatile", "A2")
  t = xlsx.evaluate(wb, "Volatile", "A1")

  print "NOW   day      = " + floor(n)
  print "NOW   secs     = " + floor((n - floor(n)) * 86400 + 0.5)
  print "TODAY          = " + t
  print "TODAY is whole = " + (t = floor(t))
  print "same day       = " + (floor(n) = t)
  ' The ordering property anything doing date math depends on.
  print "NOW > TODAY    = " + (n > t)

  print ""
  print "== the cached values are STALE, and check must not compare against them =="
  ' The fixture caches the 2025-08-01 serial, which is not when this runs. A
  ' `check` that compared would report a disagreement; the correct answer is
  ' that these two cells cannot be judged at all.
  print "cached TODAY   = " + xlsx.cell(wb, "Volatile", "A1").value
  print "cached NOW     = " + xlsx.cell(wb, "Volatile", "A2").value
  c = xlsx.check(wb, "Volatile")
  print "agree          = " + c.agree
  print "disagree       = " + c.disagree
  print "volatile       = " + c.volatile_skipped
  for each note in c.notes
    print "  " + note.verdict + "  " + note.ref + "  " + note.formula + "  computed=" + note.computed
  end for

  print ""
  print "== a volatile cell does not stop the rest of the sheet being judged =="
  ' B1 = 2+3 sits on the same sheet and is ordinary; it must still agree.
  print "B1 computed    = " + xlsx.evaluate(wb, "Volatile", "B1")
end program
