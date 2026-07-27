' PLAT-PROC: a child that never terminates on its own, ended by a polite stop.
' sleep_long.sh does not trap SIGTERM, so the first signal is enough; the whole
' process GROUP is signalled, so the inner `sleep` dies with the script.
h = process.start({ command: "tests/native_platform/helpers/sleep_long.sh" })
s = process.poll(h)
print "running-before-stop=" + s.running

s = process.stop(h, { force_after: 5 })
print "running-after-stop=" + s.running
print "signal=" + s.signal
print "exit_code=" + s.exit_code
print "success=" + s.success
process.release(h)
