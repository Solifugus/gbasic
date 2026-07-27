' PLAT-PROC deadlock proof: the child writes ~70 KB on BOTH streams, each larger
' than a 64 KB pipe buffer. If process.wait did not drain while waiting, the child
' would block writing and wait would block waiting -- a deadlock. Waiting FIRST and
' reading afterwards is therefore the strong form of this test.
h = process.start({ command: "tests/native_platform/helpers/big_streams.sh" })
s = process.wait(h)
c = process.read(h)
print "exit_code=" + s.exit_code
print "success=" + s.success
print "out_bytes=" + byte_count(c.stdout)
print "err_bytes=" + byte_count(c.stderr)
process.release(h)
