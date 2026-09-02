' Recipe 2 — Two servicers, identical loans, different delinquency. Both right.
'
'   mba — 30 days delinquent the day AFTER one payment is missed. The count is
'         PAYMENTS MISSED.
'   ots — 30 days delinquent when the oldest unpaid instalment is 30 days old.
'         The count is DAYS PAST DUE.
'
' On a monthly loan the two run about a month apart for the whole life of a
' delinquency. A book reported one way is not comparable with a book reported
' the other, so `credit` requires the method and never infers it.
program main()
  load credit

  ' A borrower who last paid in January. The February, March and April
  ' instalments are outstanding.
  feb {date}= "2026-02-01"
  mar {date}= "2026-03-01"
  apr {date}= "2026-04-01"
  unpaid = [feb, mar, apr]

  print "date         payments due   mba            ots"
  for each d in probe_dates()
    print ("  " + ymd(d) + "   " + string(due_by(unpaid, d)) + "              "
           + fill(credit.bucket(unpaid, d, "mba"), 15)
           + credit.bucket(unpaid, d, "ots"))
  next

  ' The relationship, stated rather than left to the eye: MBA is never gentler.
  ' It counts a whole missed payment as thirty days the moment it is missed,
  ' where OTS waits for the calendar to catch up.
  print ""
  print "mba is at or ahead of ots on every row above."

  ' The two agree when nothing is owed, which is the control: a `bucket` that
  ' ignored its method entirely would still pass a current-only comparison.
  jan15 {date}= "2026-01-15"
  print ""
  print "with nothing due yet:"
  print "  mba " + credit.bucket(unpaid, jan15, "mba")
  print "  ots " + credit.bucket(unpaid, jan15, "ots")

  ' There is no default. Asking for one is refused by name, because a wrong
  ' guess here shifts a whole book by a bucket and nothing on the report says so.
  on error goto next
  b = credit.bucket(unpaid, apr, "whatever")
  if error then
    print ""
    print "refused: " + error.message
    error.clear()
  end if
  on error stop
end program

function probe_dates()
  a {date}= "2026-02-02"
  b {date}= "2026-03-02"
  c {date}= "2026-04-02"
  d {date}= "2026-04-15"
  e {date}= "2026-05-15"
  return [a, b, c, d, e]
end function

function due_by(unpaid, as_of)
  n = 0
  for each u in unpaid
    if u <= as_of then
      n = n + 1
    end if
  next
  return n
end function

function ymd(d)
  return string(d.year) + "-" + two(d.month) + "-" + two(d.day)
end function

function two(n)
  if n < 10 then
    return "0" + string(n)
  end if
  return string(n)
end function

function fill(s, w)
  out = s
  while len(out) < w
    out = out + " "
  end while
  return out
end function
