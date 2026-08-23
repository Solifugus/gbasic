' process.which: the answer to "is this optional tool installed?", which could
' not previously be asked at all — process.run raises on a missing executable
' and at the time a raise could not be caught, so the attempt was fatal for
' exactly the users who did not have the tool. Catching is possible since
' PLAT-ERR; asking remains the right shape, because "is it installed" is a fact
' about the machine rather than an exceptional event. Deterministic: probes only things guaranteed by
' POSIX or constructed by the test itself.

program main(args)
  ' `sh` is guaranteed to exist on any POSIX host; where it lives varies, so
  ' assert the properties rather than the path.
  p = process.which("sh")
  print "sh found: " + string(not is_unknown(p))
  print "sh is absolute: " + string(starts_with(string(p), "/"))
  print "sh ends with /sh: " + string(ends_with(string(p), "/sh"))

  ' and what which found is genuinely runnable — the two answers must agree
  r = process.run({ command: string(p), args: ["-c", "echo ran"] })
  print "runs: " + trim(r.stdout)

  print "missing: " + string(is_unknown(process.which("gbasic-test-no-such-tool")))
  print "empty: " + string(is_unknown(process.which("")))

  ' A name with '/' is a literal path, no search — execvp's rule, mirrored.
  print "literal hit: " + string(not is_unknown(process.which(string(p))))
  print "literal miss: " + string(is_unknown(process.which("/no/such/dir/sh")))

  ' A DIRECTORY is not an executable, even though access(X_OK) says yes to it.
  ' This is the check the hand-rolled PATH walks got wrong.
  print "a directory is not a tool: " + string(is_unknown(process.which("/usr")))
end program
