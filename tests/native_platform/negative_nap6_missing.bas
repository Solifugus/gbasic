' NAP-6 negative: a missing executable is a LAUNCH failure -> clean runtime error,
' distinct from a child that ran and exited nonzero.
r = process.run({ command: "tests/native_platform/helpers/does_not_exist_xyz" })
print r.exit_code
