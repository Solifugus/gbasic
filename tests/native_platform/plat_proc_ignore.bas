' PLAT-PROC escalation: the child IGNORES SIGTERM. A polite stop must therefore
' leave it running, and only the caller's explicit escalation (force_after) may
' upgrade to SIGKILL. This is what makes escalation the CALLER's choice rather than
' a hidden policy. READY proves the trap is installed before we signal.
h = process.start({ command: "tests/native_platform/helpers/proc_ignore_term.sh" })

acc = ""
while find(acc, "READY") = nothing
  c = process.read(h)
  acc = acc + c.stdout
  sleep(0.01)
end while
print "ready=true"

s = process.stop(h)
print "after-polite-running=" + s.running
print "after-polite-signal=" + s.signal

s = process.stop(h, { force_after: 1 })
print "after-force-running=" + s.running
print "after-force-signal=" + s.signal
print "after-force-success=" + s.success
process.release(h)
