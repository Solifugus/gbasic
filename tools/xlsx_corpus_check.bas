' Run xlsx.check over every sheet of ONE workbook and print a single summary
' line. Driven by tools/xlsx_corpus_check.sh, one process per file.
'
' One process per file is deliberate and was learned the hard way: a corpus of
' this size reliably contains files that crash or hang a reader, and a single
' long-running process loses the whole run's results when one of them does.
' Per-file isolation costs process startup and buys a scan that always finishes
' with partial results and names the file that failed.
'
' Output is one line, machine-readable, so the shell driver can total it without
' parsing prose:
'
'   OK <agree> <disagree> <volatile> <unsupported> <sheets> <path>
'   ERR <path>
'
' `xlsx.check` is the oracle: an xlsx stores both the formula and the value
' Excel last computed for it, so every formula cell is checkable in isolation
' against an implementation that is not ours. The figshare corpus was re-saved
' through Excel in 2014, so those cached values are Excel's own output.
program main(args)
  if count(args) < 1 then
    print to error "usage: xlsx_corpus_check.bas <file.xlsx>"
    return nothing
  end if
  path = args[0]

  wb = xlsx.open(path)
  sheets = xlsx.sheets(wb)

  agree = 0
  disagree = 0
  vol = 0
  unsup = 0
  n = 0

  for each s in sheets
    r = xlsx.check(wb, s)
    agree = agree + r.agree
    disagree = disagree + r.disagree
    vol = vol + r.volatile_skipped
    unsup = unsup + r.unsupported
    n = n + 1
  end for

  print "OK " + string(agree) + " " + string(disagree) + " " + string(vol) + " " + string(unsup) + " " + string(n) + " " + path
end program
