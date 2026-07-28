' PLAT-STREAM: a high-volume child. Flushing more often must not drop, duplicate,
' reorder or re-chunk a single byte, so the flagged run is compared against the
' unflagged run for exact equality AND every line is checked individually.
'
' process.wait drains while it waits, so start -> wait -> read cannot deadlock
' even though the output is far larger than a pipe buffer (see plat_proc_big).
lines = "20000"
child = "tests/native_platform/plat_stream_bulk_child.bas"

h = process.start({ command: "./gbasic", args: ["--line-buffered", child, lines] })
s = process.wait(h)
c = process.read(h)
flagged = c.stdout
flagged_exit = s.exit_code
process.release(h)

h = process.start({ command: "./gbasic", args: [child, lines] })
s = process.wait(h)
c = process.read(h)
plain = c.stdout
plain_exit = s.exit_code
process.release(h)

print "flagged-exit=" + flagged_exit
print "plain-exit=" + plain_exit
print "flagged-bytes=" + byte_count(flagged)
print "identical=" + (flagged = plain)

' The separator must come in as a variable: escapes are NOT processed inside a
' modifier clause, so a literal "\n" there splits on backslash-n (DOGFOOD.md).
nl = "\n"
parts(split nl) = flagged
' The trailing newline yields one empty final element.
print "parts=" + count(parts)

bad = 0
i = 0
for each line in parts
    if i < number(lines) then
        if line != "line " + i then
            bad = bad + 1
        end if
    else
        if line != "" then
            bad = bad + 1
        end if
    end if
    i = i + 1
end for
print "malformed-lines=" + bad
