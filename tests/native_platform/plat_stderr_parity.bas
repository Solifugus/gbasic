' PLAT-STDERR: `print to error` renders exactly what `print` renders.
'
' The child emits every value shape twice, once per stream. If the two captures are
' byte-identical then the two forms agree on argument handling, value rendering,
' separators, newline placement and byte-for-byte encoding across the whole set --
' a stronger statement than any golden transcript, because it cannot drift: a
' change to `print`'s rendering moves both sides together or fails here.
r = process.run({ command: "./gbasic", args: ["tests/native_platform/plat_stderr_parity_child.bas"] })

print "exit=" + r.exit_code
print "stdout-bytes=" + byte_count(r.stdout)
print "stderr-bytes=" + byte_count(r.stderr)
print "identical=" + (r.stdout = r.stderr)

' Non-empty, so "identical" cannot be passing vacuously on two empty captures.
print "non-empty=" + (byte_count(r.stdout) > 0)

' The rendering itself, shown once, so a change to how any shape prints is visible
' in the diff rather than hidden behind a boolean.
print "--- rendering ---"
print r.stdout
print "--- end ---"
