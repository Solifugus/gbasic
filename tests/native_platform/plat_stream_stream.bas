' PLAT-STREAM: a completed `print` must reach a pipe consumer WHILE the child is
' still running when --line-buffered is set, and must NOT when it is absent
' (today's behaviour, deliberately unchanged).
'
' Both halves are decided by a gate file, never by a clock. The child publishes
' READY only after its first print statement has executed, so the single read
' below happens at a provably post-print instant; anything missing at that point
' is missing because stdio withheld it, not because we looked too early.
function probe(label, flagged)
    ready = "/tmp/gbasic_plat_stream_" + label + ".ready"
    gate = "/tmp/gbasic_plat_stream_" + label + ".gate"
    r{file} = ready
    g{file} = gate
    if exists(r) then
        delete(r)
    end if
    if exists(g) then
        delete(g)
    end if

    argv = ["tests/native_platform/plat_stream_child.bas", ready, gate]
    if flagged then
        argv = ["--line-buffered", "tests/native_platform/plat_stream_child.bas", ready, gate]
    end if
    h = process.start({ command: "./gbasic", args: argv })

    ' Give up if the child died without ever publishing READY (e.g. an interpreter
    ' that rejects the flag) -- otherwise this would spin until the runner's
    ' timeout instead of failing with a readable diff.
    while not exists(r)
        st = process.poll(h)
        if not st.running and not exists(r) then
            break
        end if
        sleep(0.01)
    end while

    ' One read, at a post-print instant. No loop, no retry: with line buffering
    ' the bytes are already in the pipe; without it they are still in the child.
    c = process.read(h)
    mid = c.stdout

    ' Release the gate; the child prints its second chunk and exits.
    write(g, "")
    s = process.wait(h)
    c = process.read(h)
    all = mid + c.stdout

    delete(r)
    delete(g)
    process.release(h)
    return { mid: mid, all: all, exit_code: s.exit_code }
end function

flagged = probe("flag", true)
plain = probe("plain", false)

print "flagged-midrun-bytes=" + byte_count(flagged.mid)
print "flagged-midrun-has-one=" + (find(flagged.mid, "CHUNK-ONE") != nothing)
print "flagged-midrun-has-two=" + (find(flagged.mid, "CHUNK-TWO") != nothing)
print "plain-midrun-bytes=" + byte_count(plain.mid)

print "flagged-exit=" + flagged.exit_code
print "plain-exit=" + plain.exit_code
print "flagged-all-bytes=" + byte_count(flagged.all)
print "plain-all-bytes=" + byte_count(plain.all)
print "totals-identical=" + (flagged.all = plain.all)
print "all=<" + flagged.all + ">"
