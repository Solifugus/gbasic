' NAP-6: the optional cwd option changes the child's working directory before exec.
' The command is resolved via PATH (`pwd`), so chdir does not affect finding it;
' `pwd -P` prints the physical directory (getcwd), proving the child ran in /tmp.
r = process.run({ command: "pwd", args: ["-P"], cwd: "/tmp" })
print r.exit_code
print r.stdout
