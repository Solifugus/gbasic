' PLAT-PROC: start a child without blocking, read its output INCREMENTALLY while it
' is still running, then wait for exit and release. The gate file turns "read arrived
' mid-run" from a race into a fact: until the parent creates the gate, the child
' cannot write CHUNK-TWO and cannot exit, so anything observed before that is
' provably mid-run no matter how slow the host (or valgrind) is.
gate = "/tmp/gbasic_plat_proc_basic.gate"
g(file) = gate
if exists(g) then
  delete(g)
end if

h = process.start({ command: "tests/native_platform/helpers/proc_gated.sh", args: [gate] })
print "type=" + type(h)

acc = ""
while find(acc, "CHUNK-ONE") = nothing
  c = process.read(h)
  acc = acc + c.stdout
  sleep(0.01)
end while

s = process.poll(h)
print "running-midrun=" + s.running
print "exit_code-midrun=" + s.exit_code
print "first=<" + acc + ">"

' Release the gate; the child writes its second chunk and exits.
write(g, "")
s = process.wait(h)
c = process.read(h)
acc = acc + c.stdout

print "running-after-wait=" + s.running
print "exit_code=" + s.exit_code
print "signal=" + s.signal
print "success=" + s.success
print "all=<" + acc + ">"

' A read after exit yields nothing more -- bytes are delivered exactly once.
c = process.read(h)
print "after-exit-bytes=" + byte_count(c.stdout)

delete(g)
process.release(h)
