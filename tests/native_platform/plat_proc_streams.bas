' process.start's stdout: and stderr: (2026-11-09): where a child's output goes.
' "pipe" is the default and the parent reads it; "null" and a file take it away
' from the pipe, which then reads end-of-file, so process.read answers "" for
' that stream; "inherit" leaves the parent's own. A detached child whose stderr
' was a pipe nobody read any more had no fd 2 that could not break (Transward;
' DOGFOOD 2026-11-09).
'
' FIRST, while the parent has printed nothing: with stdout: "inherit" the
' child writes straight onto the parent's stdout, and this golden holds it.
h = process.start({ command: "sh", args: ["-c", "echo child on the parent stdout"], stdout: "inherit" })
s = process.wait(h, 10)
r = process.read(h)
print "inherit: the pipe got [" + r.stdout + "], exit " + string(s.exit_code)
' and stderr: its line goes to the parent's stderr, which this golden does not
' hold, so the evidence is the pipe's: it reads nothing
h = process.start({ command: "sh", args: ["-c", "echo to the parent stderr >&2"], stderr: "inherit" })
s = process.wait(h, 10)
r = process.read(h)
print "inherit stderr: the pipe got [" + r.stderr + "], exit " + string(s.exit_code)

d = temp_dir()
log {file}= join_path(d, "run.log")
write(log, "already here" + chr(10))
h = process.start({ command: "sh", args: ["-c", "echo out; echo err >&2"], stderr: log })
s = process.wait(h, 10)
r = process.read(h)
print "a file: stdout [" + trim(r.stdout) + "] stderr [" + r.stderr + "]"
print "the file, appended to:"
print read(log)

h = process.start({ command: "sh", args: ["-c", "echo out; echo err >&2"], stdout: "null", stderr: "null" })
s = process.wait(h, 10)
r = process.read(h)
print "null: [" + r.stdout + "] [" + r.stderr + "] exit " + string(s.exit_code)

' a child that writes to a broken-off stderr still runs to its end
h = process.start({ command: "sh", args: ["-c", "echo gone >&2; echo still"], stderr: "null" })
s = process.wait(h, 10)
print "null stderr, stdout still read: " + trim(process.read(h).stdout)

h = process.start({ command: "sh", args: ["-c", "echo out; echo err >&2"], stdout: "pipe", stderr: "pipe" })
s = process.wait(h, 10)
r = process.read(h)
print "pipe, said: [" + trim(r.stdout) + "] [" + trim(r.stderr) + "]"

on error goto next
h = process.start({ command: "sh", args: ["-c", "true"], stderr: "run.log" })
print "a bare path: " + error.message
error.clear()
h = process.start({ command: "sh", args: ["-c", "true"], stdout: 5 })
print "a number: " + error.message
error.clear()
h = process.start({ command: "sh", args: ["-c", "true"], stdout: {file}"/nonexistent/dir/x.log" })
print "a file that cannot be opened: " + error.message
error.clear()
remove_tree(d)
