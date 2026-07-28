' PLAT-STREAM: output that does not end in a newline.
'
' Two things are checked, with and without the flag:
'   1. mid-run, a partial line (an `input` prompt) is already visible -- the
'      interpreter fflushes it explicitly, so line buffering costs nothing here;
'   2. at exit, the unterminated tail is still delivered, byte for byte.
'
' The gate file makes (1) a fact rather than a race: READY is published only after
' the prompt has been written.
function probe(label, flagged)
    ready = "/tmp/gbasic_plat_stream_partial_" + label + ".ready"
    gate = "/tmp/gbasic_plat_stream_partial_" + label + ".gate"
    r(file) = ready
    g(file) = gate
    if exists(r) then
        delete(r)
    end if
    if exists(g) then
        delete(g)
    end if

    child = "tests/native_platform/plat_stream_partial_child.bas"
    argv = [child, ready, gate]
    if flagged then
        argv = ["--line-buffered", child, ready, gate]
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

    c = process.read(h)
    mid = c.stdout

    write(g, "")
    process.wait(h)
    c = process.read(h)
    all = mid + c.stdout

    delete(r)
    delete(g)
    process.release(h)
    return { mid: mid, all: all }
end function

flagged = probe("flag", true)
plain = probe("plain", false)

print "flagged-midrun=<" + flagged.mid + ">"
print "plain-midrun=<" + plain.mid + ">"
print "midrun-identical=" + (flagged.mid = plain.mid)

print "flagged-all-bytes=" + byte_count(flagged.all)
print "plain-all-bytes=" + byte_count(plain.all)
print "totals-identical=" + (flagged.all = plain.all)
print "ends-with-newline=" + (byte_at(flagged.all, byte_count(flagged.all) - 1) = 10)
print "all=<" + flagged.all + ">"
