' PLAT-STREAM: a child that dies by signal mid-output. Nothing runs stdio cleanup
' for a signalled process, so exactly the bytes that had already left it survive.
' With --line-buffered that is the completed line; without it, nothing at all --
' which is the whole reason a supervisor cannot rely on the default.
'
' The parent reads only AFTER the child is dead, so this measures what SURVIVED,
' not what was in flight.
function probe(label, flagged)
    ready = "/tmp/gbasic_plat_stream_signal_" + label + ".ready"
    r{file} = ready
    if exists(r) then
        delete(r)
    end if

    child = "tests/native_platform/plat_stream_signal_child.bas"
    argv = [child, ready]
    if flagged then
        argv = ["--line-buffered", child, ready]
    end if
    h = process.start({ command: "./gbasic", args: argv })

    ' Give up if the child died without ever publishing READY (e.g. an interpreter
    ' that rejects the flag), so the failure is a diff rather than a hang.
    while not exists(r)
        st = process.poll(h)
        if not st.running and not exists(r) then
            break
        end if
        sleep(0.01)
    end while

    s = process.stop(h, { force_after: 5 })
    c = process.read(h)

    delete(r)
    process.release(h)
    return { out: c.stdout, signal: s.signal, running: s.running }
end function

flagged = probe("flag", true)
plain = probe("plain", false)

print "flagged-signal=" + flagged.signal
print "plain-signal=" + plain.signal
print "flagged-running=" + flagged.running
print "flagged-survived=<" + flagged.out + ">"
print "plain-survived-bytes=" + byte_count(plain.out)
