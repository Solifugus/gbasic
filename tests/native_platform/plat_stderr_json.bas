' PLAT-STDERR: a program writing to stderr must not disturb --json-diagnostics.
'
' The same child is run twice, from the same path and failing at the same line, so
' the runtime's diagnostic is emitted from an identical position both times. The
' only difference is whether the program wrote its own lines to the same stream.
' Extracting the JSON lines from each capture and comparing them answers the
' question exactly: the runtime's bytes must be identical, and still parse.
function json_lines(text)
    nl = "\n"
    out = []
    for each line in split(text, nl)
        if left(line, 1) = "{" then
            out = append(out, line)
        end if
    end for
    return out
end function

' The mode is set in the environment, so both runs name the same FILE and the
' runtime's diagnostic carries the same path, line and column in each.
child = "tests/native_platform/plat_stderr_diag_child.bas"
base = "./gbasic --json-diagnostics " + child
loud = process.run({ command: "/bin/sh", args: ["-c", "GBASIC_STDERR_LOUD=1 " + base] })
quiet = process.run({ command: "/bin/sh", args: ["-c", "GBASIC_STDERR_LOUD=0 " + base] })

print "loud-exit=" + loud.exit_code
print "quiet-exit=" + quiet.exit_code

loud_json = json_lines(loud.stderr)
quiet_json = json_lines(quiet.stderr)

print "loud-json-count=" + count(loud_json)
print "quiet-json-count=" + count(quiet_json)
print "json-identical=" + (join(loud_json, "\n") = join(quiet_json, "\n"))

' The program's own lines are present in the loud run and absent from the quiet
' one, so "identical" above is about the runtime's bytes, not about the program
' having silently failed to write anything.
print "loud-has-app=" + (find(loud.stderr, "app: starting") != nothing)
print "quiet-has-app=" + (find(quiet.stderr, "app: starting") != nothing)

' Still machine-readable: a program's writes sharing the stream must not make the
' runtime's diagnostic unparseable. The consumer separates by line, which is the
' contract --json-diagnostics already had.
if count(loud_json) > 0 then
    d = decode(loud_json[0])
    print "parsed-severity=" + d.severity
    print "parsed-code=" + d.code
    print "parsed-line=" + d.start.line
    print "parsed-message=" + d.message
end if

' And stdout is untouched by any of it.
print "loud-stdout=<" + loud.stdout + ">"
print "quiet-stdout=<" + quiet.stdout + ">"

print "--- loud stderr ---"
print loud.stderr
print "--- end ---"
