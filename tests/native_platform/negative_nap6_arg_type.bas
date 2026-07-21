' NAP-6 negative: every args element must be a string (no silent coercion).
r = process.run({ command: "echo", args: ["ok", 7] })
