' PLAT-PROC: a child that exits NON-ZERO is a normal result, not a raised error --
' matching process.run's contract. Both streams survive to the post-exit read.
h = process.start({ command: "tests/native_platform/helpers/streams.sh" })
s = process.wait(h)
c = process.read(h)
print "running=" + s.running
print "exit_code=" + s.exit_code
print "success=" + s.success
print "signal=" + s.signal
print "out=<" + c.stdout + ">"
print "err=<" + c.stderr + ">"
process.release(h)
