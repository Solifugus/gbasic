' NAP-6: argument fidelity. Every metacharacter-laden argument must reach the child
' LITERALLY — proof that process.run never invokes a shell.
r = process.run({ command: "tests/native_platform/helpers/echo_args.sh", args: ["hello world", "$HOME", "; rm -rf /", "a*b", "quote\"here", "back`tick`"] })
print r.exit_code
print r.stdout
