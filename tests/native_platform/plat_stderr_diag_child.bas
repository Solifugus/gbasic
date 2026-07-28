' PLAT-STDERR: one program, two modes, so the runtime's diagnostic is emitted from
' the byte-identical position both times.
'
'   GBASIC_STDERR_LOUD=1 -- the program writes its own lines to stderr, then fails
'   unset                -- the same program fails, having written nothing there
'
' Comparing the two runs' stderr under --json-diagnostics answers the question that
' matters: does a program writing to the diagnostic stream perturb the runtime's
' own diagnostics? Using ONE file (rather than a pair) keeps the script path, the
' failing statement's line and its column identical, so any difference between the
' two captures is caused by the program's writes and nothing else.
'
' The mode arrives through the environment rather than as a program argument
' because `--json-diagnostics` takes a FILE and nothing after it.
'
' The `print to error` calls sit behind an inline `if`, which occupies no extra
' line, so the failing statement's position does not move between modes.
loud = env("GBASIC_STDERR_LOUD") = "1"

print "OUT-before"
if loud then print to error "app: starting"
if loud then print to error "app: about to fail"

error "deliberate failure"
