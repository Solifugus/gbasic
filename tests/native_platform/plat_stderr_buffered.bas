' PLAT-STDERR x PLAT-STREAM: what --line-buffered does and does not affect.
'
' The flag calls setvbuf on stdout only. stderr is already unbuffered in this
' runtime, so a line written with `print to error` leaves the process immediately
' whether or not the flag is set -- and this probe proves exactly that, by reading
' the two streams at the same provably-post-write instant in both configurations.
'
' The gate file decides everything; no clock does. The child publishes READY only
' after both of its early writes have executed.
function probe(label, flagged)
    ready = "/tmp/gbasic_plat_stderr_" + label + ".ready"
    gate = "/tmp/gbasic_plat_stderr_" + label + ".gate"
    r(file)= ready
    g(file)= gate
    if exists(r) then
        delete(r)
    end if
    if exists(g) then
        delete(g)
    end if

    child = "tests/native_platform/plat_stderr_gate_child.bas"
    argv = [child, ready, gate]
    if flagged then
        argv = ["--line-buffered", child, ready, gate]
    end if
    h = process.start({ command: "./gbasic", args: argv })

    ' Bail out if the child died without publishing READY, so a broken build fails
    ' with a readable diff instead of spinning until the runner's timeout.
    while not exists(r)
        st = process.poll(h)
        if not st.running and not exists(r) then
            break
        end if
        sleep(0.01)
    end while

    ' One read, at a post-write instant. No retry: whatever stdio has released is
    ' already in the pipes.
    c = process.read(h)
    mid_out = c.stdout
    mid_err = c.stderr

    write(g, "")
    s = process.wait(h)
    c = process.read(h)

    if exists(r) then
        delete(r)
    end if
    if exists(g) then
        delete(g)
    end if
    process.release(h)
    return { mid_out: mid_out, mid_err: mid_err,
             all_err: mid_err + c.stderr, exit_code: s.exit_code }
end function

flagged = probe("flag", true)
plain = probe("plain", false)

' stderr arrives mid-run BOTH ways: the flag is irrelevant to it.
print "flagged-midrun-err=" + (find(flagged.mid_err, "ERR-EARLY") != nothing)
print "plain-midrun-err=" + (find(plain.mid_err, "ERR-EARLY") != nothing)

' stdout arrives mid-run only WITH the flag -- the contrast that shows the two
' streams are governed independently.
print "flagged-midrun-out=" + (find(flagged.mid_out, "OUT-EARLY") != nothing)
print "plain-midrun-out=" + (find(plain.mid_out, "OUT-EARLY") != nothing)

' Nothing later leaked into the mid-run read in either configuration.
print "flagged-midrun-has-late=" + (find(flagged.mid_err, "ERR-LATE") != nothing)
print "plain-midrun-has-late=" + (find(plain.mid_err, "ERR-LATE") != nothing)

' And the totals match, so the flag changes when bytes leave, never which bytes.
print "err-totals-identical=" + (flagged.all_err = plain.all_err)
print "all-err=<" + flagged.all_err + ">"
print "flagged-exit=" + flagged.exit_code
print "plain-exit=" + plain.exit_code
