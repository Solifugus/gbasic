' NAP-6: stdout and stderr captured SEPARATELY; a nonzero exit is a normal result
' (no runtime error raised).
r = process.run({ command: "tests/native_platform/helpers/streams.sh" })
print r.exit_code
print r.success
print "out=<" + r.stdout + ">"
print "err=<" + r.stderr + ">"
