' NAP-6: ~70 KB on BOTH streams at once (each exceeds a 64 KB pipe buffer). Exact
' byte counts prove complete capture with no deadlock.
r = process.run({ command: "tests/native_platform/helpers/big_streams.sh" })
print r.exit_code
print byte_count(r.stdout)
print byte_count(r.stderr)
