' Print just the NOW serial's day and seconds, for the table-driven epoch
' checks in tests/run_xlsx.sh. Separate from xlsx_volatile_test.bas so the
' runner can loop over many pinned instants without a golden per instant.
program main(args)
  wb = xlsx.open("examples/fixtures/xlsx/volatile.xlsx")
  n = xlsx.evaluate(wb, "Volatile", "A2")
  print floor(n)
  print floor((n - floor(n)) * 86400 + 0.5)
end program
