' PLAT-PROC: a child that cannot be launched RAISES (never a normal result), so it
' stays distinguishable from a child that ran and exited nonzero.
process.start({ command: "./gbasic-no-such-binary-plat-proc" })
