' NAP-6: a child that outlives `timeout` seconds is killed (whole process group).
' The result reports the kill coherently — not as an ordinary success.
r = process.run({ command: "tests/native_platform/helpers/sleep_long.sh", timeout: 0.3 })
print r.timed_out
print r.success
print r.exit_code
print r.signal
