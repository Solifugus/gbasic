' PLAT-STREAM: --line-buffered composes with --json-diagnostics and changes
' nothing about it. The same program is run three ways and the diagnostic stream
' must come back byte-identical every time, in either flag order.
child = "tests/native_platform/plat_stream_diag_child.bas"

function capture(argv)
    h = process.start({ command: "./gbasic", args: argv })
    s = process.wait(h)
    c = process.read(h)
    process.release(h)
    return { out: c.stdout, err: c.stderr, exit_code: s.exit_code }
end function

base = capture(["--json-diagnostics", child])
after = capture(["--json-diagnostics", "--line-buffered", child])
before = capture(["--line-buffered", "--json-diagnostics", child])

print "exit=" + base.exit_code + "/" + after.exit_code + "/" + before.exit_code
print "stderr-identical-after=" + (after.err = base.err)
print "stderr-identical-before=" + (before.err = base.err)
print "stdout-identical-after=" + (after.out = base.out)
print "stdout-identical-before=" + (before.out = base.out)
print "stdout=<" + base.out + ">"
print "stderr-is-json=" + (left(base.err, 1) = "{")
nl = "\n"
errlines{split nl} = base.err
print "stderr-lines=" + count(errlines)
