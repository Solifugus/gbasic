' A READ ERROR RAISES, it does not end the loop. A short read that looks like
' end-of-file is the file-reading form of "a directory that cannot be read is not
' an empty one" (run_dir_builtins, 2026-10-13), and it is the shape an ETL job
' cannot survive: a truncated extract silently reported as a complete one.
program main(args)
  check_raises("an absent file", "/tmp/gb_definitely_absent_12345")
  ' A DIRECTORY opened as a file: fopen SUCCEEDS on glibc and the first fread
  ' fails with EISDIR, so this is a genuine MID-STREAM read error rather than a
  ' refusal at open time -- which is the half that could have ended quietly.
  check_raises("a directory", "/tmp")
end program

function check_raises(label, path)
  f {file}= path
  n = 0
  on error goto next
  for each line in f
    n = n + 1
  end for
  if error then
    print "ok   " + label + " raises: " + error.message
    error.clear()
  else
    print "MISMATCH " + label + " did NOT raise; it reported " + string(n) + " lines"
  end if
  return nothing
end function
