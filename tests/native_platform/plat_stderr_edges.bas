' PLAT-STDERR: byte-level edge cases -- an empty argument, an embedded newline, an
' interior NUL, and a multi-byte codepoint.
r = process.run({ command: "./gbasic", args: ["tests/native_platform/plat_stderr_edge_child.bas"] })

print "exit=" + r.exit_code
print "stdout-bytes=" + byte_count(r.stdout)
print "stderr-bytes=" + byte_count(r.stderr)

' An empty argument still terminates its line: the stream opens with a bare
' newline rather than with nothing at all.
print "starts-with-newline=" + (byte_at(r.stderr, 0) = 10)

' The interior NUL survives the whole path -- rendering, the pipe, and capture.
' Byte 7: one for the empty line, two for "a", four for "x\ny".
print "has-nul=" + (byte_at(r.stderr, 7) = 0)

' The last byte is a newline: every `print to error` terminates its line, so a
' stream that ends with one is not a partial write left in a buffer.
print "ends-with-newline=" + (byte_at(r.stderr, byte_count(r.stderr) - 1) = 10)

' Byte-by-byte, so an off-by-one in any single case is visible rather than hidden
' behind a total that happens to match.
i = 0
codes = []
while i < byte_count(r.stderr)
    codes = append(codes, byte_at(r.stderr, i))
    i = i + 1
end while
print "bytes=" + codes
