' NAP-6: basic success. `echo` is resolved via PATH (execvp), argv delivered
' directly, stdout captured, exit 0, empty stderr.
r = process.run({ command: "echo", args: ["hi"] })
print r.exit_code
print r.success
print r.signal
print r.timed_out
print "out=<" + r.stdout + ">"
print "err=<" + r.stderr + ">"
