' launch_failure: "result" — a failed LAUNCH comes back as a record instead of
' a raise. Introduced when a raise could not be caught, which made attempting an
' optional tool unsafe under the default; the reason still stands on its own --
' why the launch failed arrives as DATA beside the rest of the result -- process.which answers "is it
' there", and this closes the check-then-run race for a caller who wants to
' just run it and read what happened.

program main(args)
  r = process.run({ command: "gbasic-test-no-such-tool", launch_failure: "result" })
  print "launch_failed: " + string(r.launch_failed)
  print "success: " + string(r.success)
  print "exit_code: " + string(r.exit_code)
  print "why mentions the tool: " + string(find(r.why, "gbasic-test-no-such-tool") != nothing)

  ' A child that RAN and exited 127 stays distinguishable from one that never
  ' started — the reason the exec-error pipe exists, preserved in this mode.
  r2 = process.run({ command: "sh", args: ["-c", "exit 127"], launch_failure: "result" })
  print "ran-and-127 launch_failed: " + string(r2.launch_failed)
  print "ran-and-127 exit_code: " + string(r2.exit_code)

  ' A normal success under the option carries the field too, so a caller can
  ' branch on it without has().
  r3 = process.run({ command: "sh", args: ["-c", "echo ok"], launch_failure: "result" })
  print "ok launch_failed: " + string(r3.launch_failed)
  print "ok stdout: " + trim(r3.stdout)

  ' The DEFAULT record shape is unchanged: nobody who did not opt in sees a
  ' new field, so no existing golden moves.
  r4 = process.run({ command: "sh", args: ["-c", "echo plain"] })
  print "default has launch_failed: " + string(has(r4, "launch_failed"))
end program
