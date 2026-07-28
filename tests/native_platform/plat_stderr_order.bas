' PLAT-STDERR: interleaving, when both streams share one destination.
'
' The child alternates strictly: OUT-1, ERR-1, OUT-2, ERR-2, ... With `2>&1` the
' two fds point at one pipe, so what a reader sees is decided entirely by when each
' stream flushes -- which makes this the clearest available demonstration of the
' buffering asymmetry.
'
'   with --line-buffered : source order, because stdout flushes per line and stderr
'                          is unbuffered, so both release at every line
'   without              : every ERR- line first and every OUT- line at exit,
'                          because stdout on a pipe is block-buffered and holds
'                          everything until the process ends
'
' The unflagged case is deterministic, not a race: this child's total stdout is far
' below one stdio buffer, so nothing can flush early, and both runs are complete
' before either capture is read. It is the concrete reason a gBASIC command-line
' tool that wants readable interleaved output wants the flag.
function combined(flagged)
    child = "tests/native_platform/plat_stderr_order_child.bas"
    cmd = "./gbasic " + child + " 2>&1"
    if flagged then
        cmd = "./gbasic --line-buffered " + child + " 2>&1"
    end if
    r = process.run({ command: "/bin/sh", args: ["-c", cmd] })
    return r.stdout
end function

print "--- with --line-buffered (source order) ---"
print combined(true)
print "--- without (stderr first, stdout at exit) ---"
print combined(false)
print "--- end ---"
